package setup

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/hugo/rootkitlings/internal/tui"
)

type dep struct {
	name    string
	found   bool
	version string
	path    string
}

func Run(root string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("cannot find home directory: %w", err)
	}
	base := filepath.Join(home, ".rklings")

	deps := checkDeps()
	missing := printDeps(deps)

	if missing {
		fmt.Println()
		printInstallInstructions(deps)
		return fmt.Errorf("missing dependencies")
	}

	for _, dir := range []string{"ssh", "certs", "cache"} {
		if err := os.MkdirAll(filepath.Join(base, dir), 0700); err != nil {
			return fmt.Errorf("create %s: %w", dir, err)
		}
	}

	if err := ensureSSHKey(filepath.Join(base, "ssh", "id_ed25519")); err != nil {
		return err
	}

	if err := ensureCA(filepath.Join(base, "certs")); err != nil {
		return err
	}

	if err := ensureServerCert(filepath.Join(base, "certs")); err != nil {
		return err
	}

	if err := ensureVMImage(filepath.Join(base, "cache")); err != nil {
		return err
	}

	fmt.Println()
	tui.Pass("Ready.")
	return nil
}

func checkDeps() []dep {
	var deps []dep

	// Go >= 1.22
	path, ver := findCommand("go")
	if path != "" {
		goVer := parseGoVersion(ver)
		if compareVersions(goVer, "1.22") >= 0 {
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

	// mkcert
	path, ver = findCommand("mkcert")
	if path != "" {
		deps = append(deps, dep{"mkcert", true, parseGenericVersion(ver), path})
	} else {
		deps = append(deps, dep{"mkcert", false, "", ""})
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
			fmt.Println("  Go (>= 1.22):")
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
		case "mkcert":
			fmt.Println("  mkcert:")
			printPerPlatform(
				"brew install mkcert",
				"go install filippo.io/mkcert@latest",
				"sudo pacman -S mkcert",
			)
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

func ensureCA(dir string) error {
	keyPath := filepath.Join(dir, "ca.key")
	crtPath := filepath.Join(dir, "ca.crt")

	if _, err := os.Stat(keyPath); err == nil {
		if _, err := os.Stat(crtPath); err == nil {
			tui.Pass("rklings CA          %s", crtPath)
			return nil
		}
	}

	tui.Info("Generating rklings CA...")

	pub, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return fmt.Errorf("generate CA key: %w", err)
	}

	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return fmt.Errorf("generate serial: %w", err)
	}

	template := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "rklings CA"},
		NotBefore:             time.Now(),
		NotAfter:              time.Now().Add(10 * 365 * 24 * time.Hour),
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, template, pub, priv)
	if err != nil {
		return fmt.Errorf("create CA cert: %w", err)
	}

	keyBytes, err := x509.MarshalPKCS8PrivateKey(priv)
	if err != nil {
		return fmt.Errorf("marshal CA key: %w", err)
	}

	if err := writePEM(keyPath, "PRIVATE KEY", keyBytes, 0600); err != nil {
		return err
	}
	if err := writePEM(crtPath, "CERTIFICATE", certDER, 0644); err != nil {
		return err
	}

	tui.Pass("rklings CA          %s", crtPath)
	return nil
}

func ensureServerCert(dir string) error {
	keyPath := filepath.Join(dir, "server.key")
	crtPath := filepath.Join(dir, "server.crt")

	if _, err := os.Stat(keyPath); err == nil {
		if _, err := os.Stat(crtPath); err == nil {
			tui.Pass("Server cert (mkcert) %s", crtPath)
			return nil
		}
	}

	tui.Info("Generating server cert via mkcert...")
	cmd := exec.Command("mkcert",
		"-key-file", keyPath,
		"-cert-file", crtPath,
		"localhost", "127.0.0.1", "10.0.2.2",
	)
	cmd.Stdout = nil
	cmd.Stderr = nil
	if err := cmd.Run(); err != nil {
		tui.Fail("Server cert generation failed")
		return fmt.Errorf("mkcert: %w", err)
	}
	tui.Pass("Server cert (mkcert) %s", crtPath)
	return nil
}

const VMImageName = "ubuntu-26.04-minimal-cloudimg-amd64.img"
const vmImageURL = "https://cloud-images.ubuntu.com/minimal/releases/resolute/release/" + VMImageName

const D00ImageName = "ubuntu-24.04-minimal-cloudimg-amd64.img"
const d00ImageURL = "https://cloud-images.ubuntu.com/minimal/releases/noble/release/" + D00ImageName

func ensureVMImage(cacheDir string) error {
	for _, img := range []struct{ name, url string }{
		{VMImageName, vmImageURL},
		{D00ImageName, d00ImageURL},
	} {
		imgPath := filepath.Join(cacheDir, img.name)
		if _, err := os.Stat(imgPath); err == nil {
			tui.Pass("VM image            %s", img.name)
			continue
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
		tui.Pass("VM image            %s", img.name)
	}
	return nil
}

func writePEM(path, typ string, data []byte, perm os.FileMode) error {
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, perm)
	if err != nil {
		return fmt.Errorf("write %s: %w", path, err)
	}
	defer f.Close()
	return pem.Encode(f, &pem.Block{Type: typ, Bytes: data})
}
