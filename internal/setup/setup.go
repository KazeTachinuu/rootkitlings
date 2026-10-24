package setup

import (
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/KazeTachinuu/rootkitlings/internal/build"
	"github.com/KazeTachinuu/rootkitlings/internal/project"
	"github.com/KazeTachinuu/rootkitlings/internal/tui"
)

type dep struct {
	name    string
	found   bool
	version string
	path    string
}

func Run(root string) error {
	base, err := project.DataDir()
	if err != nil {
		return fmt.Errorf("resolve project data directory: %w", err)
	}

	deps := checkDeps()
	missing := printDeps(deps)

	if missing {
		fmt.Println()
		printInstallInstructions(deps)
		return fmt.Errorf("missing dependencies")
	}

	for _, dir := range []string{"ssh", "keys", "cache"} {
		if err := os.MkdirAll(filepath.Join(base, dir), 0700); err != nil {
			return fmt.Errorf("create %s: %w", dir, err)
		}
	}

	if err := ensureSSHKey(filepath.Join(base, "ssh", "id_ed25519")); err != nil {
		return err
	}

	if err := ensureX25519Keys(filepath.Join(base, "keys")); err != nil {
		return err
	}

	if err := ensureVMImage(filepath.Join(base, "cache")); err != nil {
		return err
	}
	runtimeName, err := build.DetectRuntime()
	if err != nil {
		return err
	}
	if err := build.EnsureBuildImage(runtimeName); err != nil {
		return err
	}
	if err := build.EnsureGuestTools(root); err != nil {
		return err
	}
	count, err := build.CompileDatabase(root)
	if err != nil {
		return fmt.Errorf("generate compile database: %w", err)
	}
	tui.Pass("compile_commands.json (%d C sources)", count)

	fmt.Println()
	tui.Pass("Ready.")
	tui.Info("next: ./rklings list")
	return nil
}

func checkDeps() []dep {
	var deps []dep

	// Match the framework module's declared toolchain.
	path, ver := findCommand("go")
	if path != "" {
		goVer := parseGoVersion(ver)
		if compareVersions(goVer, "1.27") >= 0 {
			deps = append(deps, dep{"Go", true, goVer, path})
		} else {
			deps = append(deps, dep{"Go", false, goVer, path})
		}
	} else {
		deps = append(deps, dep{"Go", false, "", ""})
	}

	// Podman or Docker (need one)
	path, ver = findCommand("podman", "docker")
	if path != "" {
		name := "Podman"
		if strings.Contains(path, "docker") {
			name = "Docker"
		}
		deps = append(deps, dep{name, true, parseGenericVersion(ver), path})
	} else {
		deps = append(deps, dep{"Podman", false, "", ""})
	}

	// QEMU >= 8.0
	path, ver = findCommand("qemu-system-x86_64")
	if path != "" {
		qemuVer := parseGenericVersion(ver)
		if compareVersions(qemuVer, "8.0") >= 0 {
			deps = append(deps, dep{"QEMU", true, qemuVer, path})
		} else {
			deps = append(deps, dep{"QEMU", false, qemuVer, path})
		}
	} else {
		deps = append(deps, dep{"QEMU", false, "", ""})
	}

	for _, required := range []struct {
		label string
		names []string
	}{
		{"qemu-img", []string{"qemu-img"}},
		{"SSH client", []string{"ssh"}},
		{"SCP", []string{"scp"}},
		{"ssh-keygen", []string{"ssh-keygen"}},
		{"curl", []string{"curl"}},
		{"ISO builder", []string{"xorriso", "genisoimage", "mkisofs"}},
	} {
		found := ""
		for _, name := range required.names {
			if candidate, err := exec.LookPath(name); err == nil {
				found = candidate
				break
			}
		}
		deps = append(deps, dep{required.label, found != "", "", found})
	}

	return deps
}

func findCommand(names ...string) (string, string) {
	for _, name := range names {
		path, err := exec.LookPath(name)
		if err != nil {
			continue
		}
		out, err := exec.Command(path, "--version").CombinedOutput()
		if err != nil {
			// Go uses "go version" not "go --version"
			if name == "go" {
				out, err = exec.Command(path, "version").CombinedOutput()
				if err != nil {
					continue
				}
			} else {
				continue
			}
		}
		return path, strings.TrimSpace(string(out))
	}
	return "", ""
}

func printDeps(deps []dep) bool {
	missing := false
	for _, d := range deps {
		if d.found {
			label := d.name + " " + d.version
			tui.Pass("%-20s", label)
		} else {
			label := d.name
			if d.version != "" {
				label += " " + d.version + " (too old)"
			} else {
				label += strings.Repeat(" ", 16-len(d.name)) + "not found"
			}
			tui.Fail("%-20s", label)
			missing = true
		}
	}
	return missing
}

func printInstallInstructions(deps []dep) {
	fmt.Println("  Missing dependencies:")
	fmt.Println()

	for _, d := range deps {
		if d.found {
			continue
		}
		switch d.name {
		case "Go":
			fmt.Println("  Go (>= 1.27):")
			printPerPlatform(
				"brew install go",
				"sudo apt install golang-go",
				"sudo pacman -S go",
			)
		case "Docker":
			fmt.Println("  Docker or Podman (need one):")
			printPerPlatform(
				"brew install --cask docker",
				"sudo apt install docker.io",
				"sudo pacman -S docker",
			)
		case "QEMU":
			fmt.Println("  QEMU (>= 8.0):")
			printPerPlatform(
				"brew install qemu",
				"sudo apt install qemu-system-x86",
				"sudo pacman -S qemu-full",
			)
		default:
			fmt.Printf("  %s: install it with your system package manager.\n\n", d.name)
		}
	}
}

func printPerPlatform(macos, ubuntu, arch string) {
	switch runtime.GOOS {
	case "darwin":
		fmt.Printf("    macOS:   %s\n", macos)
	default:
		fmt.Printf("    Ubuntu:  %s\n", ubuntu)
		fmt.Printf("    Arch:    %s\n", arch)
	}
	fmt.Println()
}

// parseGoVersion extracts version from "go version go1.22.4 linux/amd64".
func parseGoVersion(s string) string {
	for _, field := range strings.Fields(s) {
		if strings.HasPrefix(field, "go") && len(field) > 2 {
			v := strings.TrimPrefix(field, "go")
			if v[0] >= '0' && v[0] <= '9' {
				return v
			}
		}
	}
	return ""
}

// parseGenericVersion finds the first token that looks like a version number.
func parseGenericVersion(s string) string {
	for _, field := range strings.Fields(s) {
		cleaned := strings.TrimRight(field, ",;)")
		if len(cleaned) > 0 && cleaned[0] >= '0' && cleaned[0] <= '9' && strings.Contains(cleaned, ".") {
			return cleaned
		}
	}
	return ""
}

// compareVersions compares dotted version strings. Returns -1, 0, or 1.
func compareVersions(a, b string) int {
	pa := strings.Split(a, ".")
	pb := strings.Split(b, ".")
	for i := 0; i < len(pa) || i < len(pb); i++ {
		va, vb := 0, 0
		if i < len(pa) {
			va, _ = strconv.Atoi(pa[i])
		}
		if i < len(pb) {
			vb, _ = strconv.Atoi(pb[i])
		}
		if va < vb {
			return -1
		}
		if va > vb {
			return 1
		}
	}
	return 0
}

func ensureSSHKey(path string) error {
	if _, err := os.Stat(path); err == nil {
		tui.Pass("SSH key             %s", path)
		return nil
	}
	tui.Info("Generating SSH key...")
	cmd := exec.Command("ssh-keygen", "-t", "ed25519", "-f", path, "-N", "", "-C", "rklings")
	cmd.Stdout = nil
	cmd.Stderr = nil
	if err := cmd.Run(); err != nil {
		tui.Fail("SSH key generation failed")
		return fmt.Errorf("ssh-keygen: %w", err)
	}
	tui.Pass("SSH key             %s", path)
	return nil
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func ensureX25519Keys(dir string) error {
	operatorKey := filepath.Join(dir, "operator.key")
	operatorPub := filepath.Join(dir, "operator.pub")
	agentKey := filepath.Join(dir, "agent.key")
	agentPub := filepath.Join(dir, "agent.pub")

	if fileExists(operatorKey) && fileExists(operatorPub) &&
		fileExists(agentKey) && fileExists(agentPub) {
		tui.Pass("X25519 keys          %s", dir)
		return nil
	}

	tui.Info("Generating X25519 ECDH keys...")
	curve := ecdh.X25519()

	opPriv, err := curve.GenerateKey(rand.Reader)
	if err != nil {
		return fmt.Errorf("generate operator key: %w", err)
	}
	agPriv, err := curve.GenerateKey(rand.Reader)
	if err != nil {
		return fmt.Errorf("generate agent key: %w", err)
	}

	if err := os.WriteFile(operatorKey, opPriv.Bytes(), 0600); err != nil {
		return fmt.Errorf("write operator.key: %w", err)
	}
	if err := os.WriteFile(operatorPub, opPriv.PublicKey().Bytes(), 0644); err != nil {
		return fmt.Errorf("write operator.pub: %w", err)
	}
	if err := os.WriteFile(agentKey, agPriv.Bytes(), 0600); err != nil {
		return fmt.Errorf("write agent.key: %w", err)
	}
	if err := os.WriteFile(agentPub, agPriv.PublicKey().Bytes(), 0644); err != nil {
		return fmt.Errorf("write agent.pub: %w", err)
	}

	tui.Pass("X25519 keys          %s", dir)
	return nil
}

const VMImageName = "ubuntu-26.04-minimal-cloudimg-amd64.img"
const vmImageURL = "https://cloud-images.ubuntu.com/minimal/releases/resolute/release/" + VMImageName

const D00ImageName = "ubuntu-24.04-minimal-cloudimg-amd64.img"
const d00ImageURL = "https://cloud-images.ubuntu.com/minimal/releases/noble/release-20251001/" + D00ImageName

func ensureVMImage(cacheDir string) error {
	for _, img := range []struct{ name, url string }{
		{VMImageName, vmImageURL},
		{D00ImageName, d00ImageURL},
	} {
		imgPath := filepath.Join(cacheDir, img.name)
		if _, err := os.Stat(imgPath); err == nil {
			if err := verifyImage(imgPath, img.url); err == nil {
				tui.Pass("VM image            %s", img.name)
				continue
			}
			_ = os.Remove(imgPath)
		}
		tui.Info("downloading %s...", img.name)
		cmd := exec.Command("curl", "-L", "-#", "-o", imgPath, img.url)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			os.Remove(imgPath)
			tui.Fail("VM image download: %s", img.name)
			return fmt.Errorf("download %s: %w", img.name, err)
		}
		if err := verifyImage(imgPath, img.url); err != nil {
			_ = os.Remove(imgPath)
			return fmt.Errorf("verify %s: %w", img.name, err)
		}
		tui.Pass("VM image            %s", img.name)
	}
	return nil
}

func verifyImage(path, imageURL string) error {
	base := imageURL[:strings.LastIndex(imageURL, "/")+1]
	client := &http.Client{Timeout: 30 * time.Second}
	response, err := client.Get(base + "SHA256SUMS")
	if err != nil {
		return fmt.Errorf("download SHA256SUMS: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("download SHA256SUMS: %s", response.Status)
	}
	sums, err := io.ReadAll(io.LimitReader(response.Body, 2<<20))
	if err != nil {
		return fmt.Errorf("read SHA256SUMS: %w", err)
	}
	name := filepath.Base(path)
	want := ""
	for _, line := range strings.Split(string(sums), "\n") {
		fields := strings.Fields(line)
		if len(fields) == 2 && strings.TrimPrefix(fields[1], "*") == name {
			want = fields[0]
			break
		}
	}
	if _, err := hex.DecodeString(want); err != nil || len(want) != sha256.Size*2 {
		return fmt.Errorf("no valid checksum for %s", name)
	}
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return err
	}
	got := hex.EncodeToString(hash.Sum(nil))
	if got != want {
		return fmt.Errorf("SHA-256 mismatch: got %s, want %s", got, want)
	}
	return nil
}
