package build

import (
	"embed"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"

	"github.com/BurntSushi/toml"
	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
	"github.com/KazeTachinuu/rootkitlings/internal/project"
	"github.com/KazeTachinuu/rootkitlings/internal/tui"
)

func relPath(p string) string {
	if wd, err := os.Getwd(); err == nil {
		if r, err := filepath.Rel(wd, p); err == nil {
			return r
		}
	}
	return p
}

//go:embed container/Containerfile container/toolchains.toml
var containerAssets embed.FS

type toolchain struct {
	ID      string `toml:"id"`
	Kernel  string `toml:"kernel"`
	Image   string `toml:"image"`
	Base    string `toml:"base"`
	Sources string `toml:"sources"`
}

type toolchainConfig struct {
	Toolchains []toolchain `toml:"toolchain"`
}

func loadToolchain(id string) (toolchain, error) {
	data, err := containerAssets.ReadFile("container/toolchains.toml")
	if err != nil {
		return toolchain{}, fmt.Errorf("read toolchain configuration: %w", err)
	}
	var config toolchainConfig
	if err := toml.Unmarshal(data, &config); err != nil {
		return toolchain{}, fmt.Errorf("parse toolchain configuration: %w", err)
	}
	for _, candidate := range config.Toolchains {
		if candidate.ID == id {
			if candidate.Kernel == "" || candidate.Image == "" || candidate.Base == "" {
				return toolchain{}, fmt.Errorf("toolchain %q is incomplete", id)
			}
			return candidate, nil
		}
	}
	return toolchain{}, fmt.Errorf("unknown toolchain %q", id)
}

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

func EnsureGuestTools(root string) error {
	dataDir, err := project.DataDir()
	if err != nil {
		return fmt.Errorf("project data directory: %w", err)
	}
	output := filepath.Join(dataDir, "build", "tools")
	if err := os.MkdirAll(output, 0700); err != nil {
		return fmt.Errorf("create guest tool directory: %w", err)
	}
	runtime, err := DetectRuntime()
	if err != nil {
		return err
	}
	if err := EnsureBuildImage(runtime); err != nil {
		return err
	}
	tc, err := loadToolchain("course")
	if err != nil {
		return err
	}
	args := []string{"run", "--rm"}
	// Rootless Podman maps container root to the invoking host user. Passing
	// the host UID again selects a subordinate UID that cannot write this mount.
	// Docker does not perform that mapping, so run it as the host user.
	if runtime == "docker" {
		args = append(args, "--user", fmt.Sprintf("%d:%d", os.Getuid(), os.Getgid()))
	}
	args = append(args,
		"-v", root+":/repo:ro",
		"-v", output+":/out",
		tc.Image, "make", "-C", "/repo", "tools", "TOOL_OUT=/out")
	cmd := exec.Command(runtime, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("build guest verification tools: %w", err)
	}
	return nil
}

func EnsureBuildImage(runtime string) error {
	tc, err := loadToolchain("course")
	if err != nil {
		return err
	}
	return ensureBuildImage(runtime, tc)
}

func ensureBuildImage(runtime string, tc toolchain) error {
	cmd := exec.Command(runtime, "image", "inspect", tc.Image)
	cmd.Stdout = nil
	cmd.Stderr = nil
	if cmd.Run() == nil {
		return nil
	}

	tui.Info("building %s image...", tc.Image)

	dir, err := project.TempDir("build-ctx-")
	if err != nil {
		return fmt.Errorf("create temp dir: %w", err)
	}
	defer os.RemoveAll(dir)

	containerfile, err := containerAssets.ReadFile("container/Containerfile")
	if err != nil {
		return fmt.Errorf("read Containerfile: %w", err)
	}
	if err := os.WriteFile(filepath.Join(dir, "Containerfile"), containerfile, 0644); err != nil {
		return fmt.Errorf("stage Containerfile: %w", err)
	}

	cmd = exec.Command(runtime, "build", "--pull=missing",
		"--build-arg", "BASE="+tc.Base,
		"--build-arg", "KERNEL="+tc.Kernel,
		"--build-arg", "SOURCES="+tc.Sources,
		"-t", tc.Image, "-f", filepath.Join(dir, "Containerfile"), dir)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("build image: %w", err)
	}

	tui.Pass("build image ready")
	return nil
}

func Build(ex *catalog.Exercise) (string, error) {
	return BuildWithVariables(ex, nil)
}

func BuildWithVariables(ex *catalog.Exercise, variables map[string]string) (string, error) {
	return buildWithVariables(ex, variables, true)
}

// BuildWithVariablesQuiet builds a validation variant without emitting the
// normal artifact progress lines. The validator reports the variant batch as
// one operation instead of exposing each internal compiler invocation.
func BuildWithVariablesQuiet(ex *catalog.Exercise, variables map[string]string) (string, error) {
	return buildWithVariables(ex, variables, false)
}

func buildWithVariables(ex *catalog.Exercise, variables map[string]string, report bool) (string, error) {
	tc, err := loadToolchain("course")
	if err != nil {
		return "", err
	}
	kernel := *ex
	if ex.KernelDir != "" {
		kernel.Source = filepath.Join(ex.Source, ex.KernelDir)
		kernel.WorkspaceFiles = filesBelow(ex.WorkspaceFiles, ex.KernelDir)
	}
	return buildKernel(&kernel, tc.Kernel, tc.Image, EnsureBuildImage, variables, report)
}

// BuildD00 builds the M01 module for D00's intentionally pinned Noble kernel.
func BuildD00(ex *catalog.Exercise) (string, error) {
	tc, err := loadToolchain("deployment")
	if err != nil {
		return "", err
	}
	deployment := *ex
	deployment.ID = ex.ID + "-deployment"
	module, err := buildKernel(&deployment, tc.Kernel, tc.Image, func(runtime string) error {
		return ensureBuildImage(runtime, tc)
	}, nil, true)
	if err != nil || module == "" {
		return module, err
	}
	root, err := project.Root()
	if err != nil {
		return "", err
	}
	asset := filepath.Join(filepath.Dir(module), "copy_fail_exp.py")
	source := filepath.Join(root, "exercises", "D00-deploy", ".rklings",
		"assets", "copy_fail_exp.py")
	if err := publishArtifact(source, asset); err != nil {
		return "", err
	}
	tui.Pass("staged %s", relPath(asset))
	return module, nil
}

func buildKernel(ex *catalog.Exercise, kernel, image string, ensure func(string) error, variables map[string]string, report bool) (string, error) {
	// Only kernel exercises (those with a Kbuild file) produce a .ko.
	kbuildPath := filepath.Join(ex.Source, "Kbuild")
	if _, err := os.Stat(kbuildPath); err != nil {
		return "", nil
	}

	rt, err := DetectRuntime()
	if err != nil {
		return "", err
	}

	if err := ensure(rt); err != nil {
		return "", err
	}

	if report {
		tui.Info("building %s...", ex.ID)
	}

	stage, err := project.TempDir("kernel-src-")
	if err != nil {
		return "", fmt.Errorf("create kernel staging directory: %w", err)
	}
	defer os.RemoveAll(stage)
	if err := stageWorkspace(stage, ex); err != nil {
		return "", fmt.Errorf("stage kernel source: %w", err)
	}
	outDir, ephemeral, err := outputDir(ex.ID, len(variables) > 0)
	if err != nil {
		return "", err
	}
	buildOut := outDir
	if !ephemeral {
		buildOut, err = project.TempDir("kernel-out-")
		if err != nil {
			return "", fmt.Errorf("create kernel output directory: %w", err)
		}
		defer os.RemoveAll(buildOut)
	}

	shellCmd := fmt.Sprintf(
		"make -C /lib/modules/%s/build M=/src modules && cp /src/rklings.ko /out/",
		kernel,
	)

	args := []string{"run", "--rm"}
	keys := make([]string, 0, len(variables))
	for key := range variables {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		args = append(args, "-e", key+"="+variables[key])
	}
	args = append(args,
		"-v", stage+":/src",
		"-v", buildOut+":/out",
		image,
		"sh", "-c", shellCmd)
	cmd := exec.Command(rt, args...)

	out, err := cmd.CombinedOutput()
	if err != nil {
		if ephemeral {
			os.RemoveAll(outDir)
		}
		return "", fmt.Errorf("build %s: %w\n%s", ex.ID, err, out)
	}

	builtPath := filepath.Join(buildOut, "rklings.ko")
	if _, err := os.Stat(builtPath); err != nil {
		if ephemeral {
			os.RemoveAll(outDir)
		}
		tui.Fail("rklings.ko not produced")
		return "", fmt.Errorf("rklings.ko not found after build")
	}
	koPath := filepath.Join(outDir, "rklings.ko")
	if !ephemeral {
		if err := publishArtifact(builtPath, koPath); err != nil {
			return "", fmt.Errorf("publish rklings.ko: %w", err)
		}
	}

	if report {
		tui.Pass("built %s", relPath(koPath))
	}
	return koPath, nil
}

func publishArtifact(source, destination string) error {
	in, err := os.Open(source)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.CreateTemp(filepath.Dir(destination), ".rklings-*")
	if err != nil {
		return err
	}
	temporary := out.Name()
	defer os.Remove(temporary)

	if err := out.Chmod(0600); err != nil {
		out.Close()
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	if err := out.Sync(); err != nil {
		out.Close()
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	return os.Rename(temporary, destination)
}

// BuildAgent builds either a lesson agent at the source root or a milestone
// agent in cmd/agent. Server binaries remain host-side and are built separately.
func BuildCourseAgent(ex *catalog.Exercise, cat *catalog.Catalog, kernelArtifact string) (string, error) {
	assets := make(map[string]string)
	if ex.EmbedModuleFrom != "" {
		base := cat.Find(ex.EmbedModuleFrom)
		if base == nil || base.Solution == "" {
			return "", fmt.Errorf("%s requires reference module %s", ex.ID, ex.EmbedModuleFrom)
		}
		module, err := Build(base.WithReference())
		if err != nil {
			return "", fmt.Errorf("build embedded module: %w", err)
		}
		assets["rklings.ko"] = module
	}
	if ex.EmbedFixture != "" {
		fixture, err := buildFixture(cat.Root, ex.EmbedFixture)
		if err != nil {
			return "", err
		}
		name := ex.EmbedFixtureAs
		if name == "" {
			name = filepath.Base(ex.EmbedFixture)
		}
		assets[name] = fixture
	}
	if ex.EmbedKernel {
		module := kernelArtifact
		if module == "" {
			kernel := *ex
			var err error
			module, err = Build(&kernel)
			if err != nil {
				return "", fmt.Errorf("build embedded final-project module: %w", err)
			}
		}
		if module == "" {
			return "", fmt.Errorf("final-project kernel did not produce rklings.ko")
		}
		assets["rklings.ko"] = module
	}
	agent := *ex
	if ex.AgentDir != "" {
		agent.Source = filepath.Join(ex.Source, ex.AgentDir)
		agent.WorkspaceFiles = filesBelow(ex.WorkspaceFiles, ex.AgentDir)
	}
	return buildAgent(&agent, assets)
}

func buildAgent(ex *catalog.Exercise, assets map[string]string) (string, error) {
	scriptPath := filepath.Join(ex.Source, "agent.sh")
	if _, err := os.Stat(scriptPath); err == nil {
		outDir, _, err := outputDir(ex.ID, false)
		if err != nil {
			return "", err
		}
		outPath := filepath.Join(outDir, "agent")
		if err := publishArtifact(scriptPath, outPath); err != nil {
			return "", fmt.Errorf("publish script agent: %w", err)
		}
		if err := os.Chmod(outPath, 0700); err != nil {
			return "", fmt.Errorf("make script agent executable: %w", err)
		}
		tui.Pass("built %s", relPath(outPath))
		return outPath, nil
	}
	if _, err := os.Stat(filepath.Join(ex.Source, "go.mod")); err != nil {
		return "", nil
	}
	packagePath := "."
	keyRel := "keys"
	if _, err := os.Stat(filepath.Join(ex.Source, "main.go")); err != nil {
		if _, err := os.Stat(filepath.Join(ex.Source, "cmd", "agent", "main.go")); err != nil {
			return "", nil
		}
		packagePath = "./cmd/agent"
		keyRel = filepath.Join("cmd", "agent", "keys")
	}
	stage, err := project.TempDir("go-src-")
	if err != nil {
		return "", fmt.Errorf("create Go staging directory: %w", err)
	}
	if err := stageWorkspace(stage, ex); err != nil {
		os.RemoveAll(stage)
		return "", fmt.Errorf("stage Go source: %w", err)
	}
	for name, source := range assets {
		data, err := os.ReadFile(source)
		if err != nil {
			os.RemoveAll(stage)
			return "", fmt.Errorf("read embedded asset %s: %w", name, err)
		}
		if err := os.WriteFile(filepath.Join(stage, name), data, 0600); err != nil {
			os.RemoveAll(stage)
			return "", fmt.Errorf("stage embedded asset %s: %w", name, err)
		}
	}

	dataDir, err := project.DataDir()
	if err != nil {
		os.RemoveAll(stage)
		return "", fmt.Errorf("project data directory: %w", err)
	}
	keySource := filepath.Join(dataDir, "keys")
	keyDest := filepath.Join(stage, keyRel)
	if err := os.MkdirAll(keyDest, 0700); err != nil {
		os.RemoveAll(stage)
		return "", fmt.Errorf("create staged key directory: %w", err)
	}
	keyFiles := []struct{ name, dir string }{
		{"agent.key", keySource},
		{"operator.pub", keySource},
	}
	for _, kf := range keyFiles {
		src := filepath.Join(kf.dir, kf.name)
		if _, err := os.Stat(src); err != nil {
			os.RemoveAll(stage)
			return "", fmt.Errorf("missing %s, run \"rklings setup\" first", src)
		}
		data, err := os.ReadFile(src)
		if err != nil {
			os.RemoveAll(stage)
			return "", fmt.Errorf("read %s: %w", src, err)
		}
		mode := os.FileMode(0600)
		if kf.name != "agent.key" {
			mode = 0644
		}
		if err := os.WriteFile(filepath.Join(keyDest, kf.name), data, mode); err != nil {
			os.RemoveAll(stage)
			return "", fmt.Errorf("stage %s: %w", kf.name, err)
		}
	}

	outDir, _, err := outputDir(ex.ID, false)
	if err != nil {
		os.RemoveAll(stage)
		return "", fmt.Errorf("create Go output directory: %w", err)
	}
	outPath := filepath.Join(outDir, "agent")
	args := []string{"build", "-trimpath"}
	if ex.AgentLDFlags != "" {
		args = append(args, "-ldflags", ex.AgentLDFlags)
	}
	args = append(args, "-o", outPath, packagePath)
	cmd := exec.Command("go", args...)
	cmd.Dir = stage
	cmd.Env = append(os.Environ(), "GOOS=linux", "GOARCH=amd64", "CGO_ENABLED=0")
	out, err := cmd.CombinedOutput()
	os.RemoveAll(stage)
	if err != nil {
		return "", fmt.Errorf("build Go agent: %w\n%s", err, out)
	}
	tui.Pass("built %s", relPath(outPath))
	return outPath, nil
}

func stageWorkspace(destination string, ex *catalog.Exercise) error {
	if ex.Reference {
		return os.CopyFS(destination, os.DirFS(ex.Source))
	}
	for _, relative := range ex.WorkspaceFiles {
		source := filepath.Join(ex.Source, relative)
		target := filepath.Join(destination, relative)
		if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
			return err
		}
		input, err := os.Open(source)
		if err != nil {
			return err
		}
		info, err := input.Stat()
		if err != nil {
			_ = input.Close()
			return err
		}
		output, err := os.OpenFile(target, os.O_CREATE|os.O_WRONLY|os.O_TRUNC,
			info.Mode().Perm())
		if err != nil {
			_ = input.Close()
			return err
		}
		_, copyErr := io.Copy(output, input)
		closeOutputErr := output.Close()
		closeInputErr := input.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeOutputErr != nil {
			return closeOutputErr
		}
		if closeInputErr != nil {
			return closeInputErr
		}
	}
	return nil
}

func filesBelow(files []string, directory string) []string {
	prefix := filepath.Clean(directory) + string(filepath.Separator)
	selected := make([]string, 0, len(files))
	for _, file := range files {
		clean := filepath.Clean(file)
		if strings.HasPrefix(clean, prefix) {
			selected = append(selected, strings.TrimPrefix(clean, prefix))
		}
	}
	return selected
}

func buildFixture(root, fixture string) (string, error) {
	outDir, _, err := outputDir("fileless-fixture", false)
	if err != nil {
		return "", fmt.Errorf("create fixture output directory: %w", err)
	}
	outPath := filepath.Join(outDir, "agent")
	source := filepath.Join(root, fixture)
	cmd := exec.Command("go", "build", "-trimpath", "-o", outPath, source)
	cmd.Env = append(os.Environ(), "GOOS=linux", "GOARCH=amd64", "CGO_ENABLED=0")
	if out, err := cmd.CombinedOutput(); err != nil {
		return "", fmt.Errorf("build fileless agent fixture: %w\n%s", err, out)
	}
	return outPath, nil
}

// BuildServer builds the host-side operator for multi-binary milestones.
func BuildServer(ex *catalog.Exercise, cat *catalog.Catalog) (string, error) {
	source := ex.Source
	if ex.ServerPath != "" {
		source = filepath.Join(cat.Root, ex.ServerPath)
	}
	if ex.ServerDir != "" {
		source = filepath.Join(source, ex.ServerDir)
	}
	packagePath := "./cmd/server"
	mainPath := filepath.Join(source, "cmd", "server", "main.go")
	if _, err := os.Stat(mainPath); err != nil {
		mainPath = filepath.Join(source, "main.go")
		if _, rootErr := os.Stat(mainPath); rootErr != nil {
			return "", nil
		}
		packagePath = "."
	}
	outDir, _, err := outputDir(ex.ID, false)
	if err != nil {
		return "", fmt.Errorf("create server output directory: %w", err)
	}
	outPath := filepath.Join(outDir, "server")
	cmd := exec.Command("go", "build", "-trimpath", "-o", outPath, packagePath)
	cmd.Dir = source
	out, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("build operator server: %w\n%s", err, out)
	}
	tui.Pass("built %s", relPath(outPath))
	return outPath, nil
}

func outputDir(id string, ephemeral bool) (string, bool, error) {
	if ephemeral {
		dir, err := project.TempDir("build-case-")
		if err != nil {
			return "", false, fmt.Errorf("create temporary output directory: %w", err)
		}
		return dir, true, nil
	}
	dataDir, err := project.DataDir()
	if err != nil {
		return "", false, fmt.Errorf("project data directory: %w", err)
	}
	dir := filepath.Join(dataDir, "build", id)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return "", false, fmt.Errorf("create artifact directory: %w", err)
	}
	return dir, false, nil
}
