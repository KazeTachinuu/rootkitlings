package vm

import (
	"context"
	"crypto/rand"
	"fmt"
	"math/big"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/KazeTachinuu/rootkitlings/internal/project"
)

type BootOpts struct {
	ImagePath    string
	OverlayDir   string
	SSHPort      int
	User         string
	ExtraPortFwd []string
	MOTD         string
	D00          bool
	BootTimeout  time.Duration
}

type VM struct {
	sshPort    int
	user       string
	keyPath    string
	cmd        *exec.Cmd
	overlay    string
	tempDir    string
	consoleLog string
	logFile    *os.File
	bootTime   time.Duration
	closeOnce  sync.Once
	closeErr   error
}

func Boot(opts BootOpts) (*VM, error) {
	start := time.Now()

	dataDir, err := project.DataDir()
	if err != nil {
		return nil, fmt.Errorf("project data directory: %w", err)
	}
	keyPath := filepath.Join(dataDir, "ssh", "id_ed25519")

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

	var seedISO, tempDir string
	if opts.D00 {
		seedISO, tempDir, err = CreateD00SeedISO(keyPath + ".pub")
	} else {
		seedISO, tempDir, err = createSeedISO(keyPath+".pub", opts.MOTD)
	}
	if err != nil {
		os.Remove(overlay)
		return nil, fmt.Errorf("seed ISO: %w", err)
	}

	netdev := fmt.Sprintf("user,id=net0,hostfwd=tcp::%d-:22", port)
	for _, fwd := range opts.ExtraPortFwd {
		netdev += ",hostfwd=" + fwd
	}

	qemuArgs := []string{
		"-m", "1024", "-smp", "2", "-nographic",
		"-drive", fmt.Sprintf("file=%s,format=qcow2,if=virtio", overlay),
		"-cdrom", seedISO,
		"-netdev", netdev,
		"-device", "virtio-net-pci,netdev=net0",
	}
	if _, err := os.Stat("/dev/kvm"); err == nil {
		qemuArgs = append([]string{"-enable-kvm", "-cpu", "host"}, qemuArgs...)
	} else if runtime.GOOS == "darwin" && runtime.GOARCH == "amd64" {
		// HVF only accelerates guests matching the host architecture.
		qemuArgs = append([]string{"-accel", "hvf", "-cpu", "host"}, qemuArgs...)
	} else if runtime.GOOS == "darwin" {
		// ARM Mac running an x86_64 guest: use software emulation.
		// thread=multi gives each vCPU its own host thread for better
		// SMP performance.  QEMU warns about the guest expecting TSO
		// while the ARM host is weakly ordered, but it inserts the
		// necessary barriers.
		qemuArgs = append([]string{"-accel", "tcg,thread=multi", "-cpu", "max"}, qemuArgs...)
	}

	cmd := exec.Command("qemu-system-x86_64", qemuArgs...)

	logPath := filepath.Join(opts.OverlayDir, "console.log")
	logFile, err := os.Create(logPath)
	if err != nil {
		os.Remove(overlay)
		os.RemoveAll(tempDir)
		return nil, fmt.Errorf("create console log: %w", err)
	}
	cmd.Stdout = logFile
	cmd.Stderr = logFile

	if err := cmd.Start(); err != nil {
		logFile.Close()
		os.Remove(overlay)
		os.RemoveAll(tempDir)
		return nil, fmt.Errorf("start qemu: %w", err)
	}

	v := &VM{
		sshPort:    port,
		user:       user,
		keyPath:    keyPath,
		cmd:        cmd,
		overlay:    overlay,
		tempDir:    tempDir,
		consoleLog: logPath,
		logFile:    logFile,
	}

	timeout := opts.BootTimeout
	if timeout == 0 {
		timeout = 90 * time.Second
		// Software emulation (TCG) on a cross-architecture host is much
		// slower than hardware-accelerated virtualization.
		if runtime.GOOS == "darwin" && runtime.GOARCH != "amd64" {
			timeout = 4 * time.Minute
		}
	}
	if err := v.waitSSH(timeout); err != nil {
		v.Close()
		return nil, fmt.Errorf("%w (console log: %s)", err, logPath)
	}
	v.bootTime = time.Since(start)

	return v, nil
}

func (v *VM) BootTime() time.Duration { return v.bootTime }
func (v *VM) ConsoleLog() string      { return v.consoleLog }
func (v *VM) SSHPort() int            { return v.sshPort }

// WaitD00 waits through first-boot package installation and the deliberate
// reboot used to select the pinned deployment-lab kernel.
func (v *VM) WaitD00() error {
	deadline := time.Now().Add(8 * time.Minute)
	for time.Now().Before(deadline) {
		out, err := v.SSH("test -f /var/lib/cloud/instance/boot-finished && systemctl is-active --quiet status-check.service && printf ready")
		if err == nil && strings.TrimSpace(out) == "ready" {
			return nil
		}
		time.Sleep(2 * time.Second)
	}
	return fmt.Errorf("D00 provisioning did not finish within 8 minutes")
}

// Reboot restarts the guest and waits until the existing SSH forward is ready
// again. The QEMU process and overlay remain the same.
func (v *VM) Reboot(timeout time.Duration) error {
	if timeout == 0 {
		timeout = 2 * time.Minute
	}
	bootID, err := v.SSH("cat /proc/sys/kernel/random/boot_id")
	if err != nil {
		return fmt.Errorf("read boot ID: %w", err)
	}
	bootID = strings.TrimSpace(bootID)
	_, _ = v.SSH("nohup sh -c 'sleep 1; reboot' >/dev/null 2>&1 &")
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		current, currentErr := v.SSH("cat /proc/sys/kernel/random/boot_id")
		if currentErr == nil && strings.TrimSpace(current) != bootID {
			return nil
		}
		time.Sleep(time.Second)
	}
	return fmt.Errorf("guest boot ID did not change after %s", timeout)
}

// SetUser changes the SSH user for subsequent SSH, SCP, and Shell calls.
func (v *VM) SetUser(user string) { v.user = user }

// SSHAs runs a command over SSH as the specified user, regardless of the
// VM's current default user.
func (v *VM) SSHAs(user, cmd string) (string, error) {
	args := v.sshArgs(fmt.Sprintf("%s@localhost", user), cmd)
	out, err := exec.Command("ssh", args...).CombinedOutput()
	return string(out), err
}

// --- cloud-init ---

const profileScript = `export TERM="${TERM:-xterm-256color}"
alias ls='ls --color=auto'
alias grep='grep --color=auto'
alias dmesg='dmesg --color=auto'
if [ "$(id -u)" -eq 0 ]; then
  PS1='\[\e[1;31m\]root@\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]# '
else
  PS1='\[\e[1;32m\]\u@\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]$ '
fi
`

func createSeedISO(pubKeyPath, motd string) (isoPath, tempDir string, err error) {
	pubKey, err := os.ReadFile(pubKeyPath)
	if err != nil {
		return "", "", fmt.Errorf("read SSH pubkey: %w", err)
	}

	tempDir, err = project.TempDir("seed-")
	if err != nil {
		return "", "", err
	}

	metaData := "instance-id: victim\nlocal-hostname: victim\n"
	if err := os.WriteFile(filepath.Join(tempDir, "meta-data"), []byte(metaData), 0644); err != nil {
		os.RemoveAll(tempDir)
		return "", "", err
	}

	keyFields := strings.Fields(string(pubKey))
	if len(keyFields) < 2 {
		os.RemoveAll(tempDir)
		return "", "", fmt.Errorf("invalid SSH public key")
	}
	// Authorized-key comments are not part of the key. Omitting them also
	// prevents content-filter exercises from accidentally hiding the key line.
	key := keyFields[0] + " " + keyFields[1]
	var ud strings.Builder
	fmt.Fprintf(&ud, "#cloud-config\n")
	fmt.Fprintf(&ud, "users:\n")
	fmt.Fprintf(&ud, "  - name: root\n    lock_passwd: false\n    ssh_authorized_keys:\n      - %s\n", key)
	fmt.Fprintf(&ud, "  - name: student\n    uid: 1000\n    shell: /bin/bash\n    sudo: []\n    ssh_authorized_keys:\n      - %s\n", key)
	fmt.Fprintf(&ud, "write_files:\n")
	fmt.Fprintf(&ud, "  - path: /etc/profile.d/rklings.sh\n    content: |\n")
	for _, line := range strings.Split(profileScript, "\n") {
		if line == "" {
			fmt.Fprintf(&ud, "\n")
		} else {
			fmt.Fprintf(&ud, "      %s\n", line)
		}
	}
	if motd != "" {
		fmt.Fprintf(&ud, "  - path: /etc/motd\n    content: |\n")
		for _, line := range strings.Split(motd, "\n") {
			if line == "" {
				fmt.Fprintf(&ud, "\n")
			} else {
				fmt.Fprintf(&ud, "      %s\n", line)
			}
		}
	}

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

func mkisofs(isoPath, srcDir string) error {
	for _, tool := range []string{"mkisofs", "genisoimage", "xorriso"} {
		var cmd *exec.Cmd
		if tool == "xorriso" {
			cmd = exec.Command(tool, "-as", "mkisofs", "-output", isoPath, "-volid", "cidata", "-joliet", "-rock", srcDir)
		} else {
			cmd = exec.Command(tool, "-output", isoPath, "-volid", "cidata", "-joliet", "-rock", srcDir)
		}
		out, err := cmd.CombinedOutput()
		if err == nil {
			return nil
		}
		_ = out
	}
	return fmt.Errorf("no ISO tool found (need mkisofs, genisoimage, or xorriso)")
}

// --- SSH ---

func (v *VM) sshArgs(extra ...string) []string {
	args := []string{
		"-i", v.keyPath,
		"-p", fmt.Sprintf("%d", v.sshPort),
		"-o", "StrictHostKeyChecking=no",
		"-o", "UserKnownHostsFile=/dev/null",
		"-o", "LogLevel=ERROR",
	}
	return append(args, extra...)
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
	return v.ShellContext(context.Background())
}

// ShellContext attaches an interactive shell whose SSH process is terminated
// when the caller is interrupted. The caller's deferred VM cleanup can then
// run normally instead of leaving QEMU and its overlay behind.
func (v *VM) ShellContext(ctx context.Context) error {
	for {
		args := v.sshArgs("-t", fmt.Sprintf("%s@localhost", v.user),
			"TERM=xterm-256color bash -il; exit 0",
		)
		cmd := exec.CommandContext(ctx, "ssh", args...)
		cmd.Stdin = os.Stdin
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		err := cmd.Run()
		if err == nil || ctx.Err() != nil {
			return err
		}
		fmt.Fprintln(os.Stderr, "\n  [*] shell disconnected; waiting for VM...")
		if waitErr := v.waitSSH(90 * time.Second); waitErr != nil {
			return err
		}
		fmt.Fprintln(os.Stderr, "  [+] VM ready; reconnecting shell")
	}
}

func (v *VM) Close() error {
	v.closeOnce.Do(func() {
		if v.cmd != nil && v.cmd.Process != nil {
			_ = v.cmd.Process.Kill()
			_ = v.cmd.Wait()
		}
		if v.logFile != nil {
			_ = v.logFile.Close()
		}
		if err := os.Remove(v.overlay); err != nil && !os.IsNotExist(err) {
			v.closeErr = err
		}
		if v.tempDir != "" {
			if err := os.RemoveAll(v.tempDir); err != nil && v.closeErr == nil {
				v.closeErr = err
			}
		}
	})
	return v.closeErr
}

func (v *VM) waitSSH(timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
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
	return fmt.Errorf("SSH not available after %s on port %d", timeout, v.sshPort)
}
