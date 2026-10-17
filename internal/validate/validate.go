package validate

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/hugo/rootkitlings/internal/build"
	"github.com/hugo/rootkitlings/internal/catalog"
	"github.com/hugo/rootkitlings/internal/state"
	"github.com/hugo/rootkitlings/internal/tui"
	"github.com/hugo/rootkitlings/internal/vm"
)

func imagePath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	p := filepath.Join(home, ".rklings", "cache", "ubuntu-26.04-minimal-cloudimg-amd64.img")
	if _, err := os.Stat(p); err != nil {
		return "", fmt.Errorf("VM image not found at %s, run \"rklings setup\" first", p)
	}
	return p, nil
}

func Run(root string, ex *catalog.Exercise) error {
	image, err := imagePath()
	if err != nil {
		return err
	}

	// Host checks run before VM steps
	var failed int
	for _, check := range ex.Checks {
		if !isHostCheck(check) {
			continue
		}
		out, err := exec.Command("sh", "-c", check.Script).CombinedOutput()
		output := strings.TrimSpace(string(out))
		if err != nil {
			tui.Fail("%s: %s", check.Label, output)
			failed++
		} else {
			tui.Pass("%s", check.Label)
		}
	}

	koPath, err := build.Build(ex)
	if err != nil {
		return fmt.Errorf("build failed: %w", err)
	}

	tmpDir, err := os.MkdirTemp("", "rklings-validate-")
	if err != nil {
		return fmt.Errorf("temp dir: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	v, err := vm.Boot(vm.BootOpts{
		ImagePath:  image,
		OverlayDir: tmpDir,
		User:       "root",
	})
	if err != nil {
		return fmt.Errorf("VM boot failed: %w", err)
	}
	defer v.Close()

	if koPath != "" {
		if err := v.SCP(koPath, "/tmp/rklings.ko"); err != nil {
			return fmt.Errorf("SCP .ko failed: %w", err)
		}
		if out, err := v.SSH("insmod /tmp/rklings.ko"); err != nil {
			return fmt.Errorf("insmod failed: %s", out)
		}
	}

	for _, check := range ex.Checks {
		if isHostCheck(check) {
			continue
		}
		if err := v.SCP(check.Script, "/tmp/check"); err != nil {
			tui.Fail("%s: failed to copy check script", check.Label)
			failed++
			continue
		}
		if _, err := v.SSH("chmod +x /tmp/check"); err != nil {
			tui.Fail("%s: chmod failed", check.Label)
			failed++
			continue
		}
		out, err := v.SSH("/tmp/check")
		output := strings.TrimSpace(out)
		if err != nil {
			tui.Fail("%s: %s", check.Label, output)
			failed++
		} else {
			tui.Pass("%s", check.Label)
		}
	}

	if failed > 0 {
		return fmt.Errorf("%d check(s) failed", failed)
	}

	if err := state.Load().MarkDone(ex.ID); err != nil {
		tui.Warn("could not save progress: %v", err)
	}
	tui.Pass("validated %s", ex.ID)
	return nil
}

func isHostCheck(check catalog.Check) bool {
	return strings.Contains(check.Script, "host_check")
}
