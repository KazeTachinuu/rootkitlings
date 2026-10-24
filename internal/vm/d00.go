package vm

import (
	_ "embed"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/KazeTachinuu/rootkitlings/internal/project"
)

const D00KernelVersion = "6.8.0-101-generic"
const d00SnapshotURL = "https://snapshot.ubuntu.com/ubuntu/20260301T000000Z/"

//go:embed assets/d00-server.py
var d00VulnService string

const d00ServiceUnit = `[Unit]
Description=Status check service
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 /opt/status/server.py
User=student
Restart=always

[Install]
WantedBy=multi-user.target
`

// CreateD00SeedISO builds a cloud-init ISO for the D00 deployment target.
// It installs a pre-patch kernel (6.8.0-101) vulnerable to CVE-2026-31431
// and a command-injection HTTP service on port 8080.
func CreateD00SeedISO(pubKeyPath string) (isoPath, tempDir string, err error) {
	pubKey, err := os.ReadFile(pubKeyPath)
	if err != nil {
		return "", "", fmt.Errorf("read SSH pubkey: %w", err)
	}

	tempDir, err = project.TempDir("d00-seed-")
	if err != nil {
		return "", "", err
	}

	metaData := "instance-id: rklings-d00\nlocal-hostname: target\n"
	if err := os.WriteFile(filepath.Join(tempDir, "meta-data"), []byte(metaData), 0644); err != nil {
		os.RemoveAll(tempDir)
		return "", "", err
	}

	key := strings.TrimSpace(string(pubKey))
	var ud strings.Builder
	fmt.Fprintf(&ud, "#cloud-config\n")
	fmt.Fprintf(&ud, "users:\n")
	fmt.Fprintf(&ud, "  - name: root\n    lock_passwd: false\n    ssh_authorized_keys:\n      - %s\n", key)
	fmt.Fprintf(&ud, "  - name: student\n    uid: 1000\n    shell: /bin/bash\n    sudo: []\n")

	fmt.Fprintf(&ud, "package_update: false\n")
	fmt.Fprintf(&ud, "apt:\n")
	fmt.Fprintf(&ud, "  sources:\n")
	fmt.Fprintf(&ud, "    snapshot:\n")
	fmt.Fprintf(&ud, "      source: 'deb [trusted=yes] %s noble-updates main'\n", d00SnapshotURL)
	fmt.Fprintf(&ud, "packages:\n")
	fmt.Fprintf(&ud, "  - linux-image-%s\n", D00KernelVersion)
	fmt.Fprintf(&ud, "  - linux-modules-%s\n", D00KernelVersion)

	fmt.Fprintf(&ud, "write_files:\n")

	fmt.Fprintf(&ud, "  - path: /opt/status/server.py\n    permissions: '0755'\n    content: |\n")
	for _, line := range strings.Split(d00VulnService, "\n") {
		if line == "" {
			fmt.Fprintf(&ud, "\n")
		} else {
			fmt.Fprintf(&ud, "      %s\n", line)
		}
	}

	fmt.Fprintf(&ud, "  - path: /etc/systemd/system/status-check.service\n    content: |\n")
	for _, line := range strings.Split(d00ServiceUnit, "\n") {
		if line == "" {
			fmt.Fprintf(&ud, "\n")
		} else {
			fmt.Fprintf(&ud, "      %s\n", line)
		}
	}

	fmt.Fprintf(&ud, "  - path: /etc/default/grub.d/99-rklings.cfg\n    content: |\n")
	fmt.Fprintf(&ud, "      GRUB_DEFAULT=\"Advanced options for Ubuntu>Ubuntu, with Linux %s\"\n", D00KernelVersion)

	fmt.Fprintf(&ud, "  - path: /etc/apt/apt.conf.d/99-no-auto-update\n    content: |\n")
	fmt.Fprintf(&ud, "      APT::Periodic::Update-Package-Lists \"0\";\n")
	fmt.Fprintf(&ud, "      APT::Periodic::Unattended-Upgrade \"0\";\n")

	fmt.Fprintf(&ud, "runcmd:\n")
	fmt.Fprintf(&ud, "  - update-grub\n")
	fmt.Fprintf(&ud, "  - systemctl daemon-reload\n")
	fmt.Fprintf(&ud, "  - systemctl enable status-check.service\n")
	fmt.Fprintf(&ud, "  - reboot\n")

	if err := os.WriteFile(filepath.Join(tempDir, "user-data"), []byte(ud.String()), 0644); err != nil {
		os.RemoveAll(tempDir)
		return "", "", err
	}

	isoPath = filepath.Join(tempDir, "seed.iso")
	if err := mkisofs(isoPath, tempDir); err != nil {
		os.RemoveAll(tempDir)
		return "", "", err
	}

	return isoPath, tempDir, nil
}
