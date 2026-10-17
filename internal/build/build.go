package build

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/hugo/rootkitlings/internal/catalog"
	"github.com/hugo/rootkitlings/internal/tui"
)

const (
	targetKernel = "7.0.0-38-generic"
	buildImage   = "rklings-build:" + targetKernel
)

func DetectRuntime() (string, error) {
	for _, rt := range []string{"podman", "docker"} {
		if _, err := exec.LookPath(rt); err == nil {
			// Verify the runtime is actually usable
			if exec.Command(rt, "info").Run() == nil {
				return rt, nil
			}
		}
	}
	return "", fmt.Errorf("neither docker nor podman found (or accessible) in PATH")
}

func EnsureBuildImage(runtime string) error {
	cmd := exec.Command(runtime, "image", "inspect", buildImage)
	cmd.Stdout = nil
	cmd.Stderr = nil
	if cmd.Run() == nil {
		return nil
	}

	tui.Info("building %s image...", buildImage)

	dir, err := os.MkdirTemp("", "rklings-build-ctx-")
	if err != nil {
		return fmt.Errorf("create temp dir: %w", err)
	}
	defer os.RemoveAll(dir)

	dockerfile := fmt.Sprintf(
		"FROM ubuntu:26.04\nRUN apt-get update && apt-get install -y make gcc linux-headers-%s && rm -rf /var/lib/apt/lists/*\n",
		targetKernel,
	)
	if err := os.WriteFile(filepath.Join(dir, "Dockerfile"), []byte(dockerfile), 0644); err != nil {
		return fmt.Errorf("write Dockerfile: %w", err)
	}

	cmd = exec.Command(runtime, "build", "-t", buildImage, dir)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("build image: %w", err)
	}

	tui.Pass("build image ready")
	return nil
}

func Build(ex *catalog.Exercise) (string, error) {
	rt, err := DetectRuntime()
	if err != nil {
		return "", err
	}

	if err := EnsureBuildImage(rt); err != nil {
		return "", err
	}

	tui.Info("building %s...", ex.ID)

	outDir, err := os.MkdirTemp("", "rklings-ko-")
	if err != nil {
		return "", fmt.Errorf("create output dir: %w", err)
	}

	shellCmd := fmt.Sprintf(
		"make -C /lib/modules/%s/build M=/src modules && cp /src/rklings.ko /out/",
		targetKernel,
	)

	cmd := exec.Command(rt, "run", "--rm",
		"-v", ex.Source+":/src",
		"-v", outDir+":/out",
		buildImage,
		"sh", "-c", shellCmd,
	)

	out, err := cmd.CombinedOutput()
	if err != nil {
		os.Stderr.Write(out)
		os.RemoveAll(outDir)
		return "", fmt.Errorf("build %s: %w", ex.ID, err)
	}

	koPath := filepath.Join(outDir, "rklings.ko")
	if _, err := os.Stat(koPath); err != nil {
		os.RemoveAll(outDir)
		tui.Fail("rklings.ko not produced")
		return "", fmt.Errorf("rklings.ko not found after build")
	}

	tui.Pass("built %s", koPath)
	return koPath, nil
}
