package vm

import (
	"crypto/rand"
	"fmt"
	"math/big"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

type BootOpts struct {
	ImagePath    string
	OverlayDir   string
	SSHPort      int
	User         string
	ExtraPortFwd []string
}

type VM struct {
	sshPort int
	user    string
	keyPath string
	cmd     *exec.Cmd
	overlay string
	seedISO string
	tempDir string
}

func Boot(opts BootOpts) (*VM, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return nil, fmt.Errorf("home dir: %w", err)
	}
	keyPath := filepath.Join(home, ".rklings", "ssh", "id_ed25519")

	port := opts.SSHPort
	if port == 0 {
		n, err := rand.Int(rand.Reader, big.NewInt(50000))
		if err != nil {
			return nil, fmt.Errorf("random port: %w", err)
		}
		port = int(n.Int64()) + 10000
	}

	user := opts.User
	if user == "" {
		user = "root"
	}

	overlay := filepath.Join(opts.OverlayDir, "rklings-overlay.qcow2")
	out, err := exec.Command("qemu-img", "create", "-f", "qcow2",
		"-b", opts.ImagePath, "-F", "qcow2", overlay).CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("create overlay: %s: %w", out, err)
	}

	seedISO, tempDir, err := createSeedISO(keyPath + ".pub")
	if err != nil {
		os.Remove(overlay)
		return nil, fmt.Errorf("seed ISO: %w", err)
	}

	netdev := fmt.Sprintf("user,id=net0,hostfwd=tcp::%d-:22", port)
	for _, fwd := range opts.ExtraPortFwd {
		netdev += "," + fwd
	}

	cmd := exec.Command("qemu-system-x86_64",
		"-m", "512", "-smp", "2", "-nographic",
		"-drive", fmt.Sprintf("file=%s,format=qcow2", overlay),
		"-drive", fmt.Sprintf("file=%s,format=raw", seedISO),
		"-netdev", netdev,
		"-device", "virtio-net-pci,netdev=net0",
		"-serial", "mon:stdio",
	)

	if err := cmd.Start(); err != nil {
		os.Remove(overlay)
		os.RemoveAll(tempDir)
		return nil, fmt.Errorf("start qemu: %w", err)
	}

	v := &VM{
		sshPort: port,
		user:    user,
		keyPath: keyPath,
		cmd:     cmd,
		overlay: overlay,
		seedISO: seedISO,
		tempDir: tempDir,
	}

	if err := v.waitSSH(); err != nil {
		v.Close()
		return nil, err
	}

	return v, nil
}

func createSeedISO(pubKeyPath string) (isoPath, tempDir string, err error) {
	pubKey, err := os.ReadFile(pubKeyPath)
	if err != nil {
		return "", "", fmt.Errorf("read SSH pubkey: %w", err)
	}

	tempDir, err = os.MkdirTemp("", "rklings-seed-")
	if err != nil {
		return "", "", err
	}

	metaData := "instance-id: rklings\nlocal-hostname: rklings\n"
	if err := os.WriteFile(filepath.Join(tempDir, "meta-data"), []byte(metaData), 0644); err != nil {
		os.RemoveAll(tempDir)
		return "", "", err
	}

	userData := fmt.Sprintf(`#cloud-config
users:
  - name: root
    lock_passwd: false
    ssh_authorized_keys:
      - %s
  - name: student
    uid: 1000
    shell: /bin/bash
    sudo: []
    ssh_authorized_keys:
      - %s
`, strings.TrimSpace(string(pubKey)), strings.TrimSpace(string(pubKey)))

	if err := os.WriteFile(filepath.Join(tempDir, "user-data"), []byte(userData), 0644); err != nil {
		os.RemoveAll(tempDir)
		return "", "", err
	}

	isoPath = filepath.Join(tempDir, "seed.iso")

	out, err := exec.Command("mkisofs",
		"-output", isoPath, "-volid", "cidata", "-joliet", "-rock", tempDir).CombinedOutput()
	if err != nil {
		out, err = exec.Command("genisoimage",
			"-output", isoPath, "-volid", "cidata", "-joliet", "-rock", tempDir).CombinedOutput()
		if err != nil {
			os.RemoveAll(tempDir)
			return "", "", fmt.Errorf("create seed ISO: %s: %w", out, err)
		}
	}

	return isoPath, tempDir, nil
}

func (v *VM) sshArgs(extra ...string) []string {
	args := []string{
		"-i", v.keyPath,
		"-p", fmt.Sprintf("%d", v.sshPort),
		"-o", "StrictHostKeyChecking=no",
		"-o", "UserKnownHostsFile=/dev/null",
		"-o", "LogLevel=ERROR",
	}
	args = append(args, extra...)
	return args
}

func (v *VM) SSH(cmd string) (string, error) {
	args := v.sshArgs(fmt.Sprintf("%s@localhost", v.user), cmd)
	out, err := exec.Command("ssh", args...).CombinedOutput()
	return string(out), err
}

func (v *VM) SCP(localPath, remotePath string) error {
	args := []string{
		"-i", v.keyPath,
		"-P", fmt.Sprintf("%d", v.sshPort),
		"-o", "StrictHostKeyChecking=no",
		"-o", "UserKnownHostsFile=/dev/null",
		"-o", "LogLevel=ERROR",
		localPath,
		fmt.Sprintf("%s@localhost:%s", v.user, remotePath),
	}
	out, err := exec.Command("scp", args...).CombinedOutput()
	if err != nil {
		return fmt.Errorf("scp: %s: %w", out, err)
	}
	return nil
}

func (v *VM) Shell() error {
	args := v.sshArgs("-t", fmt.Sprintf("%s@localhost", v.user))
	cmd := exec.Command("ssh", args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func (v *VM) Close() error {
	if v.cmd != nil && v.cmd.Process != nil {
		v.cmd.Process.Kill()
		v.cmd.Wait()
	}
	os.Remove(v.overlay)
	if v.tempDir != "" {
		os.RemoveAll(v.tempDir)
	}
	return nil
}

func (v *VM) waitSSH() error {
	deadline := time.Now().Add(60 * time.Second)
	for time.Now().Before(deadline) {
		args := v.sshArgs(
			"-o", "ConnectTimeout=2",
			fmt.Sprintf("%s@localhost", v.user),
			"true",
		)
		if err := exec.Command("ssh", args...).Run(); err == nil {
			return nil
		}
		time.Sleep(2 * time.Second)
	}
	return fmt.Errorf("SSH not available after 60s on port %d", v.sshPort)
}
