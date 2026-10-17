package validate

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/hugo/rootkitlings/internal/build"
	"github.com/hugo/rootkitlings/internal/catalog"
	"github.com/hugo/rootkitlings/internal/setup"
	"github.com/hugo/rootkitlings/internal/state"
	"github.com/hugo/rootkitlings/internal/tui"
	"github.com/hugo/rootkitlings/internal/vm"
)


func imagePath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	p := filepath.Join(home, ".rklings", "cache", setup.VMImageName)
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

	// Always boot as root: SCP and insmod require it.
	v, err := vm.Boot(vm.BootOpts{
		ImagePath:  image,
		OverlayDir: tmpDir,
		User:       "root",
	})
	if err != nil {
		return fmt.Errorf("VM boot failed: %w", err)
	}
	defer v.Close()

	// Prepare VM directories and support files for check scripts.
	if _, err := v.SSH("mkdir -p /vm/checks /vm/tools"); err != nil {
		return fmt.Errorf("mkdir /vm: %w", err)
	}

	// Copy vm/ui.sh (sourced by lib.sh for colored markers).
	uiSh := filepath.Join(root, "vm", "ui.sh")
	if err := v.SCP(uiSh, "/vm/ui.sh"); err != nil {
		return fmt.Errorf("SCP ui.sh: %w", err)
	}

	// Copy shared check library.
	libSh := filepath.Join(root, "vm", "checks", "lib.sh")
	if err := v.SCP(libSh, "/vm/checks/lib.sh"); err != nil {
		return fmt.Errorf("SCP lib.sh: %w", err)
	}

	// Copy exercise-specific verification tools.
	if len(ex.Tools) > 0 {
		home, _ := os.UserHomeDir()
		toolsDir := filepath.Join(home, ".rklings", "build", "tools")
		for _, tool := range ex.Tools {
			src := filepath.Join(toolsDir, tool)
			dst := "/vm/tools/" + tool
			if err := v.SCP(src, dst); err != nil {
				return fmt.Errorf("SCP tool %s: %w", tool, err)
			}
			if _, err := v.SSH(fmt.Sprintf("chmod +x %s", dst)); err != nil {
				return fmt.Errorf("chmod tool %s: %w", tool, err)
			}
		}
	}

	if koPath != "" {
		if err := v.SCP(koPath, "/tmp/rklings.ko"); err != nil {
			return fmt.Errorf("SCP .ko failed: %w", err)
		}
		if ex.PreloadModule {
			if out, err := v.SSH("insmod /tmp/rklings.ko"); err != nil {
				return fmt.Errorf("insmod failed: %s", out)
			}
		}
	}

	// Run VM checks as the exercise's designated user.
	vmUser := ex.VMUser
	if vmUser == "" {
		vmUser = "root"
	}

	for _, check := range ex.Checks {
		if isHostCheck(check) {
			continue
		}
		remotePath := fmt.Sprintf("/tmp/check-%s", check.ID)
		if err := v.SCP(check.Script, remotePath); err != nil {
			tui.Fail("%s: failed to copy check script", check.Label)
			failed++
			continue
		}
		if _, err := v.SSH(fmt.Sprintf("chmod +x %s", remotePath)); err != nil {
			tui.Fail("%s: chmod failed", check.Label)
			failed++
			continue
		}
		out, err := v.SSHAs(vmUser, remotePath)
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
