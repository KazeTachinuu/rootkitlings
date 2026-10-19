package validate

import (
	"context"
	"debug/elf"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/KazeTachinuu/rootkitlings/internal/build"
	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
	"github.com/KazeTachinuu/rootkitlings/internal/labserver"
	"github.com/KazeTachinuu/rootkitlings/internal/operator"
	"github.com/KazeTachinuu/rootkitlings/internal/project"
	"github.com/KazeTachinuu/rootkitlings/internal/setup"
	"github.com/KazeTachinuu/rootkitlings/internal/state"
	"github.com/KazeTachinuu/rootkitlings/internal/tui"
	"github.com/KazeTachinuu/rootkitlings/internal/vm"
)

func imagePath(name string) (string, error) {
	dataDir, err := project.DataDir()
	if err != nil {
		return "", err
	}
	p := filepath.Join(dataDir, "cache", name)
	if _, err := os.Stat(p); err != nil {
		return "", fmt.Errorf("VM image not found at %s, run \"rklings setup\" first", p)
	}
	return p, nil
}

func Run(root string, ex *catalog.Exercise, cat *catalog.Catalog) error {
	return RunCase(root, ex, cat, "")
}

// RunCase validates all cases, or one named case when caseID is non-empty.
// Cases share one disposable VM so focused and full validation remain fast.
func RunCase(root string, ex *catalog.Exercise, cat *catalog.Catalog, caseID string) error {
	if err := CheckWorkspace(root, ex); err != nil {
		return err
	}
	if caseID != "" {
		selected := make([]catalog.Check, 0, 1)
		for _, check := range ex.Checks {
			if check.ID == caseID {
				selected = append(selected, check)
				break
			}
		}
		if len(selected) == 0 {
			return fmt.Errorf("%s has no validation case %q", ex.ID, caseID)
		}
		copy := *ex
		copy.Checks = selected
		ex = &copy
	}
	dataDir, err := project.DataDir()
	if err != nil {
		return fmt.Errorf("project data directory: %w", err)
	}
	imageName := setup.VMImageName
	if ex.Kind == "deployment" {
		imageName = setup.D00ImageName
	}
	image, err := imagePath(imageName)
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

	caseArtifacts := make(map[string]string)
	defer func() {
		seen := make(map[string]bool)
		for _, artifact := range caseArtifacts {
			dir := filepath.Dir(artifact)
			if !seen[dir] && isCaseArtifactDir(dataDir, artifact) {
				_ = os.RemoveAll(dir)
				seen[dir] = true
			}
		}
	}()
	type buildResult struct {
		check    catalog.Check
		artifact string
		err      error
	}
	buildChecks := make([]catalog.Check, 0, len(ex.Checks))
	for _, check := range ex.Checks {
		if len(check.BuildVariables) > 0 || check.ExpectBuild == "fail" {
			buildChecks = append(buildChecks, check)
		}
	}
	buildResults := make(map[string]buildResult, len(buildChecks))
	if len(buildChecks) > 0 {
		workers := min(len(buildChecks), min(runtime.NumCPU(), 3))
		tui.Info("building %d validation variants with %d worker(s)...",
			len(buildChecks), workers)
		jobs := make(chan catalog.Check)
		results := make(chan buildResult, len(buildChecks))
		var group sync.WaitGroup
		for range workers {
			group.Add(1)
			go func() {
				defer group.Done()
				for check := range jobs {
					artifact, buildErr := build.BuildWithVariablesQuiet(ex,
						check.BuildVariables)
					results <- buildResult{check: check, artifact: artifact,
						err: buildErr}
				}
			}()
		}
		go func() {
			for _, check := range buildChecks {
				jobs <- check
			}
			close(jobs)
			group.Wait()
			close(results)
		}()
		for result := range results {
			buildResults[result.check.ID] = result
		}
	}
	for _, check := range buildChecks {
		result := buildResults[check.ID]
		artifact, buildErr := result.artifact, result.err
		if check.ExpectBuild == "fail" {
			if buildErr == nil {
				tui.Fail("%s: build unexpectedly succeeded", check.Label)
				failed++
			} else if check.BuildOutputContains != "" && !strings.Contains(buildErr.Error(), check.BuildOutputContains) {
				tui.Fail("%s: build failed without %q", check.Label, check.BuildOutputContains)
				failed++
			} else {
				tui.Pass("%s", check.Label)
			}
			continue
		}
		if buildErr != nil {
			return fmt.Errorf("build %s case %s: %w", ex.ID, check.ID, buildErr)
		}
		if artifact != "" {
			caseArtifacts[check.ID] = artifact
		}
	}
	if len(buildChecks) > 0 {
		tui.Pass("validation variants ready")
	}
	var koPath string
	if ex.ValidationBaseCase != "" {
		koPath = caseArtifacts[ex.ValidationBaseCase]
		if koPath == "" {
			return fmt.Errorf("validation base case %q produced no module",
				ex.ValidationBaseCase)
		}
	}

	buildEx := ex
	if ex.Kind == "deployment" {
		base := cat.Find(ex.ModuleFrom)
		if base == nil || base.Solution == "" {
			return fmt.Errorf("%s requires reference module %q", ex.ID, ex.ModuleFrom)
		}
		buildEx = base.WithReference()
	}
	if ex.Kind == "deployment" {
		koPath, err = build.BuildD00(buildEx)
	} else if koPath == "" {
		koPath, err = build.Build(buildEx)
	}
	if err != nil {
		return fmt.Errorf("build failed: %w", err)
	}
	agentPath, err := build.BuildCourseAgent(ex, cat, koPath)
	if err != nil {
		return fmt.Errorf("agent build failed: %w", err)
	}
	if agentPath != "" && strings.Contains(ex.AgentLDFlags, "-s") && strings.Contains(ex.AgentLDFlags, "-w") {
		binary, err := elf.Open(agentPath)
		if err != nil {
			return fmt.Errorf("inspect hardened agent: %w", err)
		}
		hasSymbols := binary.Section(".symtab") != nil
		hasDebug := binary.Section(".debug_info") != nil
		_ = binary.Close()
		if hasSymbols || hasDebug {
			return fmt.Errorf("hardened agent retains symbol or DWARF sections")
		}
		tui.Pass("agent symbol and DWARF sections stripped")
	}
	var serverPath string
	if ex.ServeKind == "operator" {
		serverPath, err = build.BuildServer(ex, cat)
		if err != nil {
			return fmt.Errorf("operator server build failed: %w", err)
		}
	}
	if ex.ValidationLoadKernel && koPath == "" {
		base := cat.Find(ex.ModuleFrom)
		if base == nil || base.Solution == "" {
			return fmt.Errorf("%s requires reference module %q", ex.ID, ex.ModuleFrom)
		}
		koPath, err = build.Build(base.WithReference())
		if err != nil {
			return fmt.Errorf("build module dependency: %w", err)
		}
	}

	var lessonLab *labserver.Server
	var operatorProcess *operator.Process
	labAgentAddr := ""
	relayURL := ""
	if ex.ServeKind == "operator" {
		operatorProcess, err = operator.StartValidation(context.Background(), serverPath, root, filepath.Join(dataDir, "certs"))
		if err != nil {
			return fmt.Errorf("start operator validation server: %w", err)
		}
		defer operatorProcess.Close()
		labAgentAddr = operatorProcess.AgentAddr
		relayURL = operatorProcess.RelayURL
	} else if ex.Harness || ex.ServeKind == "lab" {
		certDir := filepath.Join(dataDir, "certs")
		lessonLab, err = labserver.Start(labserver.Config{
			ExerciseID:    ex.ID,
			Title:         ex.Title,
			Code:          ex.Code,
			Commands:      ex.Commands,
			Transfer:      ex.Transfer,
			HandshakeOnly: ex.HandshakeOnly,
			Welcome:       ex.Welcome,
			CertFile:      filepath.Join(certDir, "server.crt"),
			KeyFile:       filepath.Join(certDir, "server.key"),
			CAFile:        filepath.Join(certDir, "ca.crt"),
			TLSAddr:       ":0",
			HTTPAddr:      "127.0.0.1:0",
		})
		if err != nil {
			return fmt.Errorf("start %s validation server: %w", ex.ID, err)
		}
		defer lessonLab.Close()
		labAgentAddr = lessonLab.AgentAddr()
		relayURL = lessonLab.RelayURL()
	}

	tmpDir, err := project.TempDir("validate-")
	if err != nil {
		return fmt.Errorf("temp dir: %w", err)
	}
	defer os.RemoveAll(tmpDir)

	// Always boot as root: SCP and insmod require it.
	var bootTimeout time.Duration
	if ex.Kind == "deployment" {
		bootTimeout = 3 * time.Minute
	}
	v, err := vm.Boot(vm.BootOpts{
		ImagePath:   image,
		OverlayDir:  tmpDir,
		User:        "root",
		D00:         ex.Kind == "deployment",
		BootTimeout: bootTimeout,
	})
	if err != nil {
		return fmt.Errorf("VM boot failed: %w", err)
	}
	defer v.Close()
	if ex.Kind == "deployment" {
		tui.Info("waiting for D00 provisioning...")
		if err := v.WaitD00(); err != nil {
			return fmt.Errorf("D00 provisioning failed: %w", err)
		}
	}

	// Prepare VM directories and support files for check scripts.
	if _, err := v.SSH("mkdir -p /vm/checks"); err != nil {
		return fmt.Errorf("mkdir /vm: %w", err)
	}
	if labAgentAddr != "" {
		if _, err := v.SSH("printf '%s\\n' '" + labAgentAddr + "' > /vm/checks/server-addr"); err != nil {
			return fmt.Errorf("stage lab server address: %w", err)
		}
		if _, err := v.SSH("printf '%s\n' '" + relayURL + "' > /vm/checks/relay-url"); err != nil {
			return fmt.Errorf("stage relay URL: %w", err)
		}
	}
	if ex.PKI {
		if _, err := v.SSH("mkdir -p /root/certs"); err != nil {
			return fmt.Errorf("prepare PKI directory: %w", err)
		}
		for _, name := range []string{"ca.crt", "server.crt", "server.key", "agent.crt", "agent.key"} {
			source := filepath.Join(dataDir, "certs", name)
			for _, destination := range []string{"/root/" + name, "/tmp/" + name} {
				if err := v.SCP(source, destination); err != nil {
					return fmt.Errorf("stage PKI file %s: %w", name, err)
				}
			}
		}
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
		for _, toolID := range ex.Tools {
			binPath, ok := cat.ToolBinaries[toolID]
			if !ok {
				tui.Warn("unknown tool %s, skipping", toolID)
				continue
			}
			src := filepath.Join(root, binPath)
			if _, err := os.Stat(src); err != nil {
				src = filepath.Join(dataDir, "build", "tools", filepath.Base(binPath))
			}
			dst := "/usr/local/bin/" + filepath.Base(binPath)
			if err := v.SCP(src, dst); err != nil {
				return fmt.Errorf("SCP tool %s: %w", toolID, err)
			}
			if _, err := v.SSH(fmt.Sprintf("chmod +x %s", dst)); err != nil {
				return fmt.Errorf("chmod tool %s: %w", toolID, err)
			}
		}
	}

	remoteChecks := make(map[string]string)
	for _, check := range ex.Checks {
		if isHostCheck(check) || check.ExpectBuild == "fail" {
			continue
		}
		remotePath := fmt.Sprintf("/vm/checks/case-%s", check.ID)
		if err := v.SCP(check.Script, remotePath); err != nil {
			return fmt.Errorf("stage check %s: %w", check.ID, err)
		}
		if _, err := v.SSH("chmod +x " + remotePath); err != nil {
			return fmt.Errorf("prepare check %s: %w", check.ID, err)
		}
		remoteChecks[check.ID] = remotePath
	}

	// Embedded modules are host-side build inputs, not target artifacts.
	stageKernel := koPath != "" && ex.Kind != "deployment" &&
		!(ex.EmbedKernel && agentPath != "")
	if stageKernel {
		if _, err := v.SSH("mkdir -p /rklings"); err != nil {
			return fmt.Errorf("mkdir /rklings: %w", err)
		}
		if err := v.SCP(koPath, "/rklings/rklings.ko"); err != nil {
			return fmt.Errorf("SCP .ko failed: %w", err)
		}
		for _, sourceFile := range ex.SourceFiles {
			destination := filepath.Join("/rklings", filepath.Clean(sourceFile))
			if _, err := v.SSH("mkdir -p " + filepath.Dir(destination)); err != nil {
				return fmt.Errorf("prepare source-policy directory: %w", err)
			}
			if err := v.SCP(filepath.Join(ex.Source, sourceFile), destination); err != nil {
				return fmt.Errorf("stage source-policy file %s: %w", sourceFile, err)
			}
		}
		if ex.ValidationLoadKernel && agentPath != "" {
			if out, err := v.SSH("insmod /rklings/rklings.ko"); err != nil {
				return fmt.Errorf("insmod failed: %s", out)
			}
		}
	}
	if agentPath != "" {
		artifactPath := ex.ArtifactPath
		if artifactPath == "" {
			artifactPath = "/rklings/agent"
		}
		if _, err := v.SSH("mkdir -p " + filepath.Dir(artifactPath)); err != nil {
			return fmt.Errorf("prepare artifact directory: %w", err)
		}
		if err := v.SCP(agentPath, artifactPath); err != nil {
			return fmt.Errorf("SCP agent failed: %w", err)
		}
		if _, err := v.SSH("chmod 0700 " + artifactPath); err != nil {
			return fmt.Errorf("chmod agent: %w", err)
		}
		env := ""
		if ex.Harness {
			env = "env RKLINGS_SERVER_ADDR=" + labAgentAddr + " "
		}
		switch ex.Runtime {
		case "once":
			if out, err := v.SSH(env + artifactPath); err != nil {
				return fmt.Errorf("run exercise artifact: %s: %w", out, err)
			}
		case "background":
			logPath := "/tmp/agent-start.log"
			startAgent := fmt.Sprintf("nohup %s%s >%s 2>&1 </dev/null &", env, artifactPath, logPath)
			if ex.RemoveArtifact {
				startAgent += " pid=$!; sleep 1; rm -f " + artifactPath +
					"; if ! kill -0 $pid 2>/dev/null; then cat " + logPath +
					"; exit 1; fi; rm -f " + logPath
			}
			if out, err := v.SSH(startAgent); err != nil {
				return fmt.Errorf("start lesson agent: %s: %w", out, err)
			}
		}
	}

	// Check scripts always run as root: they need insmod/rmmod/dmesg access.
	// The scripts themselves use su/run_as for student-perspective tests.
	for _, check := range ex.Checks {
		if isHostCheck(check) || check.ExpectBuild == "fail" {
			continue
		}
		if artifact := caseArtifacts[check.ID]; artifact != "" {
			if err := v.SCP(artifact, "/rklings/rklings.ko"); err != nil {
				tui.Fail("%s: failed to stage case build", check.Label)
				failed++
				continue
			}
		}
		if check.Prepare != "" {
			if out, err := v.SSH(check.Prepare); err != nil {
				tui.Fail("%s: prepare failed: %s", check.Label, strings.TrimSpace(out))
				failed++
				continue
			}
		}
		remotePath := remoteChecks[check.ID]
		out, err := v.SSH(remotePath)
		output := strings.TrimSpace(out)
		if err != nil {
			tui.Fail("%s", check.Label)
			for _, line := range diagnosticLines(output) {
				tui.Detail("%s", line)
			}
			if logPath, logErr := writeFailureLog(dataDir, ex.Code, check.ID,
				output); logErr == nil {
				if relative, relErr := filepath.Rel(root, logPath); relErr == nil {
					tui.Detail("full log: %s", relative)
				} else {
					tui.Detail("full log: %s", logPath)
				}
			}
			failed++
		} else {
			tui.Pass("%s", check.Label)
		}
		if check.RebootAfter && err == nil {
			tui.Info("rebooting guest after %s...", check.ID)
			if rebootErr := v.Reboot(2 * time.Minute); rebootErr != nil {
				return fmt.Errorf("reboot after %s: %w", check.ID, rebootErr)
			}
			tui.Pass("guest rebooted")
		}
	}

	if failed > 0 {
		return fmt.Errorf("%d check(s) failed", failed)
	}

	if shouldMarkProgress(ex) {
		if err := state.Load().MarkDone(ex.ID); err != nil {
			tui.Warn("could not save progress: %v", err)
		}
	}
	tui.Pass("validated %s", ex.ID)
	return nil
}

func shouldMarkProgress(ex *catalog.Exercise) bool {
	return !ex.Reference && os.Getenv("RKLINGS_NO_PROGRESS") == ""
}

func isCaseArtifactDir(dataDir, artifact string) bool {
	dir := filepath.Dir(artifact)
	return strings.HasPrefix(filepath.Base(dir), "build-case-") &&
		filepath.Clean(filepath.Dir(dir)) ==
			filepath.Clean(filepath.Join(dataDir, "tmp"))
}

func isHostCheck(check catalog.Check) bool {
	return strings.Contains(check.Script, "host_check")
}
