package main

import (
	"context"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"text/tabwriter"
	"time"

	"github.com/KazeTachinuu/rootkitlings/internal/build"
	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
	"github.com/KazeTachinuu/rootkitlings/internal/labserver"
	"github.com/KazeTachinuu/rootkitlings/internal/operator"
	"github.com/KazeTachinuu/rootkitlings/internal/project"
	isetup "github.com/KazeTachinuu/rootkitlings/internal/setup"
	"github.com/KazeTachinuu/rootkitlings/internal/state"
	"github.com/KazeTachinuu/rootkitlings/internal/tui"
	"github.com/KazeTachinuu/rootkitlings/internal/validate"
	"github.com/KazeTachinuu/rootkitlings/internal/vm"
	"github.com/spf13/cobra"
)

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	root := &cobra.Command{
		Use:               "rklings",
		Short:             "Build and validate the Rootkitlings course",
		SilenceUsage:      true,
		DisableAutoGenTag: true,
	}
	root.CompletionOptions.DisableDefaultCmd = true

	root.AddCommand(
		listCmd(),
		setupCmd(),
		compileDBCmd(),
		buildCmd(),
		runCmd(),
		sshCmd(),
		serveCmd(),
		validateCmd(),
		statusCmd(),
		solutionCmd(),
		resetCmd(),
	)

	if err := root.ExecuteContext(ctx); err != nil {
		os.Exit(1)
	}
}

func compileDBCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "compile-db",
		Short: "Generate compile_commands.json for kernel sources",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, _ []string) {
			count, err := build.CompileDatabase(findRoot())
			if err != nil {
				tui.Fatal("compile database: %v", err)
			}
			tui.Pass("compile_commands.json (%d C sources)", count)
		},
	}
}

func sshCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "ssh [lab] [user]",
		Short: "Open another shell in the running lab",
		Args:  cobra.MaximumNArgs(2),
		Run: func(cmd *cobra.Command, args []string) {
			user := "student"
			name := ""
			if len(args) == 1 {
				if args[0] == "root" || args[0] == "student" {
					user = args[0]
				} else {
					name = args[0]
				}
			} else if len(args) == 2 {
				name, user = args[0], args[1]
			}
			if !validSSHUser(user) {
				tui.Fatal("invalid SSH user %q", user)
			}
			lab, err := state.FindLab(name)
			if err != nil {
				tui.Fatal("ssh: %v; start one with \"./rklings run <exercise>\"", err)
			}
			if lab.Exercise == "D00" {
				tui.Fatal("D00 has no SSH entry point; gain access through http://127.0.0.1:8088")
			}
			sshArgs := []string{
				"-t", "-i", lab.KeyPath,
				"-p", strconv.Itoa(lab.SSHPort),
				"-o", "StrictHostKeyChecking=no",
				"-o", "UserKnownHostsFile=/dev/null",
				"-o", "LogLevel=ERROR",
				user + "@localhost",
				"TERM=xterm-256color exec bash -il",
			}
			ssh := exec.CommandContext(cmd.Context(), "ssh", sshArgs...)
			ssh.Stdin, ssh.Stdout, ssh.Stderr = os.Stdin, os.Stdout, os.Stderr
			if err := ssh.Run(); err != nil && cmd.Context().Err() == nil {
				tui.Fatal("ssh to %s: %v", lab.Exercise, err)
			}
		},
	}
}

func validSSHUser(user string) bool {
	if user == "" || len(user) > 32 {
		return false
	}
	for _, r := range user {
		if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' ||
			r >= '0' && r <= '9' || r == '_' || r == '-' || r == '.' {
			continue
		}
		return false
	}
	return true
}

func listCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "list",
		Short: "List exercises in course order",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, _ []string) {
			cat, _ := loadCatalog()
			out := tabwriter.NewWriter(cmd.OutOrStdout(), 0, 4, 2, ' ', 0)
			progress := state.Load()
			fmt.Fprintln(out, "CODE\tSTATUS\tTITLE")
			for _, exercise := range cat.Exercises {
				status := "-"
				if progress.IsDone(exercise.ID) {
					status = "done"
				}
				fmt.Fprintf(out, "%s\t%s\t%s\n", exercise.Code, status, exercise.Title)
			}
			_ = out.Flush()
		},
	}
}

func findRoot() string {
	dir, err := os.Getwd()
	if err != nil {
		tui.Fatal("cannot determine working directory: %v", err)
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "course.toml")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			tui.Fatal("course.toml not found (are you inside the rootkitlings repo?)")
		}
		dir = parent
	}
}

func loadCatalog() (*catalog.Catalog, string) {
	root := findRoot()
	cat, err := catalog.Load(root)
	if err != nil {
		tui.Fatal("load catalog: %v", err)
	}
	return cat, root
}

func findExercise(cat *catalog.Catalog, name string) *catalog.Exercise {
	ex := cat.Find(name)
	if ex == nil {
		tui.Fatal("unknown exercise: %s", name)
	}
	return ex
}

func setupCmd() *cobra.Command {
	var serverName string
	c := &cobra.Command{
		Use:   "setup",
		Short: "Check dependencies and initialize environment",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, args []string) {
			root := findRoot()
			if err := isetup.Run(root, serverName); err != nil {
				tui.Fatal("%v", err)
			}
		},
	}
	c.Flags().StringVar(&serverName, "server-name", "localhost", "DNS name or IP included in the mTLS server certificate")
	return c
}

func buildCmd() *cobra.Command {
	var useRef bool
	c := &cobra.Command{
		Use:   "build <exercise>",
		Short: "Build all artifacts for an exercise",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, root := loadCatalog()
			ex := findExercise(cat, args[0])
			if useRef {
				if ex.Solution == "" {
					tui.Fatal("no reference solution for %s", ex.ID)
				}
				ex = ex.WithReference()
			}
			if err := validate.CheckWorkspace(root, ex); err != nil {
				tui.Fatal("%v", err)
			}
			var koPath string
			var err error
			if ex.Kind == "deployment" {
				base := findExercise(cat, ex.ModuleFrom)
				koPath, err = build.BuildD00(base.WithReference())
			} else {
				koPath, err = build.Build(ex)
			}
			if err != nil {
				tui.Fatal("%v", err)
			}
			agentPath, err := build.BuildCourseAgent(ex, cat, koPath)
			if err != nil {
				tui.Fatal("%v", err)
			}
			serverPath, err := build.BuildServer(ex, cat)
			if err != nil {
				tui.Fatal("%v", err)
			}
			if koPath == "" && agentPath == "" && serverPath == "" {
				tui.Fatal("no supported build target in %s", ex.Source)
			}
		},
	}
	c.Flags().BoolVar(&useRef, "reference", false, "build against the reference solution")
	return c
}

func runCmd() *cobra.Command {
	var useRef bool
	var instanceName string
	c := &cobra.Command{
		Use:   "run <exercise>",
		Short: "Build, boot VM, and open interactive shell",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, root := loadCatalog()
			ex := findExercise(cat, args[0])
			if instanceName != "" && !validInstanceName(instanceName) {
				tui.Fatal("invalid instance name %q (use letters, numbers, '.', '_' or '-')", instanceName)
			}

			if useRef {
				if ex.Solution == "" {
					tui.Fatal("no reference solution for %s", ex.ID)
				}
				ex = ex.WithReference()
			}
			if err := validate.CheckWorkspace(root, ex); err != nil {
				tui.Fatal("%v", err)
			}

			var koPath, agentPath string
			if ex.Kind == "deployment" {
				base := findExercise(cat, ex.ModuleFrom)
				if base.Solution == "" {
					tui.Fatal("reference module %s is required for %s", ex.ModuleFrom, ex.ID)
				}
				var err error
				koPath, err = build.BuildD00(base.WithReference())
				if err != nil {
					tui.Fatal("build deployment module: %v", err)
				}
				tui.Pass("deployment module ready on host: %s", koPath)
			} else {
				var err error
				if ex.Stages("kernel") {
					koPath, err = build.Build(ex)
					if err != nil {
						tui.Fatal("build: %v", err)
					}
				}
				if ex.Stages("agent") {
					agentPath, err = build.BuildCourseAgent(ex, cat, koPath)
					if err != nil {
						tui.Fatal("build agent: %v", err)
					}
				}
				if ex.Loads("kernel") && koPath == "" {
					base := findExercise(cat, ex.ModuleFrom)
					koPath, err = build.Build(base.WithReference())
					if err != nil {
						tui.Fatal("build module dependency: %v", err)
					}
				}
			}

			dataDir, err := project.DataDir()
			if err != nil {
				tui.Fatal("project data directory: %v", err)
			}

			agentAddr := ""
			if ex.ServeKind != "" {
				agentAddr = operator.LabAgentAddr
			}

			// D00 uses the 24.04 image; everything else uses the 26.04 image.
			imageName := isetup.VMImageName
			if ex.Kind == "deployment" {
				imageName = isetup.D00ImageName
			}
			imagePath := filepath.Join(dataDir, "cache", imageName)
			if _, err := os.Stat(imagePath); err != nil {
				tui.Fatal("VM image is not prepared; run \"./rklings setup\" first")
			}

			tmpDir, err := project.TempDir("run-")
			if err != nil {
				tui.Fatal("temp dir: %v", err)
			}
			defer os.RemoveAll(tmpDir)

			vmUser := ex.VMUser
			if vmUser == "" {
				vmUser = "root"
			}

			// D00 runs 24.04 with kernel 6.8.0-101.
			kernelLabel := "7.0.0-38"
			if ex.Kind == "deployment" {
				kernelLabel = "6.8.0-101"
			}
			motd := fmt.Sprintf(
				"%s: %s\nkernel %s | user: %s | exit to teardown\n",
				ex.ID, ex.Title, kernelLabel, vmUser,
			)

			// Keep D00 separate from the operator dashboard on host port 8080.
			var extraFwd []string
			var bootTimeout time.Duration
			if ex.Kind == "deployment" {
				extraFwd = []string{"tcp::8088-:8080"}
				bootTimeout = 3 * time.Minute
			}

			// Always boot as root: SCP needs write access to /tmp and
			// insmod requires root.  We switch to the exercise user for
			// the interactive shell afterwards.
			tui.Info("booting VM...")
			v, err := vm.Boot(vm.BootOpts{
				ImagePath:    imagePath,
				OverlayDir:   tmpDir,
				User:         "root",
				MOTD:         motd,
				ExtraPortFwd: extraFwd,
				D00:          ex.Kind == "deployment",
				BootTimeout:  bootTimeout,
			})
			if err != nil {
				tui.Fatal("VM boot: %v", err)
			}
			defer v.Close()
			failRun := func(message string, args ...any) {
				_ = v.Close()
				_ = os.RemoveAll(tmpDir)
				tui.Fatal(message, args...)
			}
			if instanceName == "" {
				instanceName = strings.ToLower(ex.Code) + "-" + strconv.Itoa(v.SSHPort())
			}
			lab := state.Lab{
				Name:     instanceName,
				Exercise: ex.Code,
				SSHPort:  v.SSHPort(),
				KeyPath:  filepath.Join(dataDir, "ssh", "id_ed25519"),
				OwnerPID: os.Getpid(),
				Started:  time.Now(),
			}
			if err := state.SaveLab(lab); err != nil {
				failRun("record running lab: %v", err)
			}
			defer state.ClearLab(os.Getpid())
			tui.Pass("VM ready (%.1fs)", v.BootTime().Seconds())
			if ex.Kind != "deployment" {
				tui.Info("additional shell: ./rklings ssh %s [student|root]", instanceName)
			}
			if ex.RunPKI {
				for _, name := range []string{"ca.crt", "server.crt", "server.key", "agent.crt", "agent.key"} {
					if err := v.SCP(filepath.Join(dataDir, "certs", name), "/root/"+name); err != nil {
						failRun("stage PKI file %s: %v", name, err)
					}
				}
			}
			if ex.Kind == "deployment" {
				tui.Info("provisioning D00 target (first boot may reboot once)...")
				if err := v.WaitD00(); err != nil {
					failRun("D00 provisioning: %v", err)
				}
				tui.Pass("vulnerable service ready at http://127.0.0.1:8088")
			}

			if koPath != "" && ex.Kind != "deployment" {
				if out, err := v.SSH("mkdir -p /rklings"); err != nil {
					failRun("prepare module directory: %v: %s", err, out)
				}
				if err := v.SCP(koPath, "/rklings/rklings.ko"); err != nil {
					failRun("stage module: %v", err)
				}
				tui.Pass("module staged at /rklings/rklings.ko")
				if ex.Loads("kernel") {
					if out, err := v.SSH("insmod /rklings/rklings.ko"); err != nil {
						failRun("load module: %v: %s", err, out)
					} else {
						tui.Pass("module loaded")
					}
				}
			}
			stagedAgentPath := ""
			if agentPath != "" {
				artifactPath := ex.RunArtifactPath
				if artifactPath == "" {
					artifactPath = "/rklings/agent"
				}
				stagedAgentPath = artifactPath
				if _, err := v.SSH("mkdir -p " + filepath.Dir(artifactPath)); err != nil {
					failRun("prepare agent directory: %v", err)
				}
				if err := v.SCP(agentPath, artifactPath); err != nil {
					failRun("stage agent: %v", err)
				}
				if _, err := v.SSH("chmod 0700 " + artifactPath); err != nil {
					failRun("prepare agent: %v", err)
				}
				if ex.ServeKind == "standalone" {
					tui.Pass("server staged at %s", artifactPath)
				} else {
					tui.Pass("agent staged at %s", artifactPath)
				}
				env := "env RKLINGS_AGENT_ID=" + instanceName + " "
				if agentAddr != "" {
					env += "RKLINGS_SERVER_ADDR=" + agentAddr + " "
				}
				runtimeMode := ex.RunAgent
				switch runtimeMode {
				case "once":
					if out, err := v.SSH(env + artifactPath); err != nil {
						failRun("run agent: %v: %s", err, out)
					}
					tui.Pass("exercise artifact completed")
				case "background":
					logPath := "/tmp/rklings-agent.log"
					if ex.RunRemove {
						logPath = "/dev/null"
					}
					startAgent := fmt.Sprintf("nohup %s%s >%s 2>&1 </dev/null &", env, artifactPath, logPath)
					if ex.RunRemove {
						startAgent += " pid=$!; sleep 1; rm -f " + artifactPath + "; kill -0 $pid"
					}
					if out, err := v.SSH(startAgent); err != nil {
						failRun("start lesson agent: %v: %s", err, out)
					}
					if ex.RunRemove {
						tui.Pass("fileless agent started")
					} else {
						tui.Pass("live agent started (log: /tmp/rklings-agent.log)")
					}
				}
			}

			// Switch to the exercise user for the interactive shell.
			v.SetUser(vmUser)
			if ex.ServeKind != "" {
				fmt.Println()
				tui.Info("host server is separate: ./rklings serve %s", ex.Code)
				if ex.ServeKind == "standalone" {
					tui.Info("guest mode is optional: %s", stagedAgentPath)
				} else if ex.RunAgent == "manual" {
					tui.Info("start the staged agent when ready:")
					fmt.Printf("      setsid -f env RKLINGS_AGENT_ID=%s %s </dev/null >/dev/null 2>&1\n", instanceName, stagedAgentPath)
				} else {
					tui.Info("the agent will connect when the host server is listening")
				}
				fmt.Println()
			}

			if ex.Kind == "deployment" {
				if out, err := v.SSHAs("root", "rm -f /root/.ssh/authorized_keys /home/student/.ssh/authorized_keys"); err != nil {
					failRun("seal deployment target: %v: %s", err, out)
				}
				tui.Pass("target sealed; initial access is HTTP only")
				tui.Info("transfer the host module only after gaining a shell")
				tui.Info("target running; press Ctrl+C to stop")
				<-cmd.Context().Done()
				return
			}

			if err := v.ShellContext(cmd.Context()); err != nil && cmd.Context().Err() == nil {
				tui.Warn("shell exited: %v", err)
			}
		},
	}
	c.Flags().BoolVar(&useRef, "reference", false, "build against the reference solution")
	c.Flags().StringVar(&instanceName, "name", "", "name this VM and operator target")
	return c
}

func validInstanceName(name string) bool {
	if name == "" || len(name) > 64 {
		return false
	}
	for _, r := range name {
		if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' ||
			r >= '0' && r <= '9' || r == '.' || r == '_' || r == '-' {
			continue
		}
		return false
	}
	return true
}

func validateCmd() *cobra.Command {
	var useRef bool
	var caseID string
	var all bool
	c := &cobra.Command{
		Use:   "validate [exercise]",
		Short: "Validate exercise solution",
		Args: func(_ *cobra.Command, args []string) error {
			if all && len(args) == 0 {
				return nil
			}
			if !all && len(args) == 1 {
				return nil
			}
			return fmt.Errorf("provide one exercise, or use --all")
		},
		Run: func(cmd *cobra.Command, args []string) {
			cat, root := loadCatalog()
			if all {
				if caseID != "" {
					tui.Fatal("--case requires one exercise")
				}
				if err := validateAll(cmd.Context(), root, cat); err != nil {
					tui.Fatal("%v", err)
				}
				return
			}
			ex := findExercise(cat, args[0])
			if useRef {
				if ex.Solution == "" {
					tui.Fatal("no reference solution for %s", ex.ID)
				}
				ex = ex.WithReference()
			}
			if err := validate.RunCase(root, ex, cat, caseID); err != nil {
				tui.Fatal("%v", err)
			}
		},
	}
	c.Flags().BoolVar(&useRef, "reference", false, "validate the reference solution")
	c.Flags().StringVar(&caseID, "case", "", "run one validation case")
	c.Flags().BoolVar(&all, "all", false, "validate every reference solution")
	return c
}

type validationResult struct {
	exercise catalog.Exercise
	output   []byte
	err      error
}

func validateAll(ctx context.Context, root string, cat *catalog.Catalog) error {
	executable, err := os.Executable()
	if err != nil {
		return fmt.Errorf("locate rklings executable: %w", err)
	}
	workers := validationWorkers()
	tui.Info("validating %d exercises with %d automatic worker(s)", len(cat.Exercises), workers)

	jobs := make(chan catalog.Exercise)
	results := make(chan validationResult)
	var group sync.WaitGroup
	for range workers {
		group.Add(1)
		go func() {
			defer group.Done()
			for exercise := range jobs {
				child := exec.CommandContext(ctx, executable, "validate", exercise.Code, "--reference")
				child.Dir = root
				child.Env = append(os.Environ(), "RKLINGS_NO_PROGRESS=1")
				output, childErr := child.CombinedOutput()
				results <- validationResult{exercise: exercise, output: output, err: childErr}
			}
		}()
	}
	go func() {
		defer close(jobs)
		for _, exercise := range cat.Exercises {
			select {
			case jobs <- exercise:
			case <-ctx.Done():
				return
			}
		}
	}()
	go func() {
		group.Wait()
		close(results)
	}()

	failed := 0
	for result := range results {
		if len(result.output) > 0 {
			fmt.Fprintf(os.Stdout, "\n[%s] %s\n%s", result.exercise.Code,
				result.exercise.Title, result.output)
			if result.output[len(result.output)-1] != '\n' {
				fmt.Fprintln(os.Stdout)
			}
		}
		if result.err != nil {
			failed++
			tui.Fail("%s failed", result.exercise.Code)
		} else {
			tui.Pass("%s passed", result.exercise.Code)
		}
	}
	if ctx.Err() != nil {
		return ctx.Err()
	}
	if failed != 0 {
		return fmt.Errorf("%d of %d exercises failed", failed, len(cat.Exercises))
	}
	return nil
}

func validationWorkers() int {
	workers := runtime.NumCPU() / 2
	if workers < 1 {
		workers = 1
	}
	if workers > 4 {
		workers = 4
	}
	data, err := os.ReadFile("/proc/meminfo")
	if err != nil {
		return workers
	}
	for _, line := range strings.Split(string(data), "\n") {
		fields := strings.Fields(line)
		if len(fields) < 2 || fields[0] != "MemAvailable:" {
			continue
		}
		availableKiB, parseErr := strconv.ParseUint(fields[1], 10, 64)
		if parseErr == nil {
			memoryWorkers := int(availableKiB / (2 * 1024 * 1024))
			if memoryWorkers < 1 {
				memoryWorkers = 1
			}
			if workers > memoryWorkers {
				workers = memoryWorkers
			}
		}
		break
	}
	return workers
}

func statusCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "status",
		Short: "Show exercise progress",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			prog := state.Load()
			completed := 0
			next := ""
			for _, ex := range cat.Exercises {
				if prog.IsDone(ex.ID) {
					completed++
					tui.Pass("%s  %s", ex.Code, ex.Title)
				} else {
					tui.Pending("%s  %s", ex.Code, ex.Title)
					if next == "" {
						next = ex.Code
					}
				}
			}
			fmt.Printf("\n  %d/%d complete\n", completed, len(cat.Exercises))
			if next != "" {
				fmt.Printf("  [*] next: ./rklings run %s\n", next)
			} else {
				fmt.Println("  [+] course complete")
			}
		},
	}
}

func solutionCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "solution <exercise>",
		Short: "Show reference solution (requires prior validation)",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])
			prog := state.Load()

			if !prog.IsDone(ex.ID) {
				tui.Fatal("validate %s first before viewing the solution", ex.ID)
			}

			files, err := solutionFiles(ex.Solution)
			if err != nil {
				tui.Fatal("read solution: %v", err)
			}
			for _, rel := range files {
				path := filepath.Join(ex.Solution, rel)
				fmt.Printf("--- %s ---\n", rel)
				data, err := os.ReadFile(path)
				if err != nil {
					tui.Warn("read %s: %v", rel, err)
					continue
				}
				fmt.Print(string(data))
				if len(data) > 0 && data[len(data)-1] != '\n' {
					fmt.Println()
				}
			}
		},
	}
}

func solutionFiles(root string) ([]string, error) {
	var files []string
	err := filepath.WalkDir(root, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() || entry.Name() == "go.sum" {
			return nil
		}
		relative, err := filepath.Rel(root, path)
		if err != nil {
			return err
		}
		files = append(files, relative)
		return nil
	})
	return files, err
}

func resetCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "reset <exercise>",
		Short: "Restore an exercise's learner files",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, root := loadCatalog()
			ex := findExercise(cat, args[0])

			if err := resetWorkspace(root, ex); err != nil {
				tui.Fatal("reset workspace: %v", err)
			}
			prog := state.Load()
			if err := prog.Clear(ex.ID); err != nil {
				tui.Warn("clear progress: %v", err)
			}

			tui.Pass("reset %s", ex.ID)
		},
	}
}

func resetWorkspace(root string, ex *catalog.Exercise) error {
	paths := make([]string, 0, len(ex.WorkspaceFiles))
	for _, file := range ex.WorkspaceFiles {
		path, err := filepath.Rel(root, filepath.Join(ex.Source, file))
		if err != nil || path == ".." || strings.HasPrefix(path, ".."+string(filepath.Separator)) {
			return fmt.Errorf("exercise workspace is outside the repository")
		}
		paths = append(paths, path)
	}
	if len(paths) == 0 {
		return nil
	}
	gitCmd := exec.Command("git", append([]string{"checkout", "--"}, paths...)...)
	gitCmd.Dir = root
	if output, err := gitCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("git checkout: %w: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func serveCmd() *cobra.Command {
	var listenAddr, webAddr string
	var useRef bool
	c := &cobra.Command{
		Use:   "serve <exercise>",
		Short: "Start an exercise's host-side server",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, root := loadCatalog()
			ex := findExercise(cat, args[0])
			if ex.ServeKind == "" {
				tui.Fatal("%s has no host-side server", ex.ID)
			}
			buildExercise := ex
			if useRef {
				buildExercise = ex.WithReference()
			}
			dataDir, err := project.DataDir()
			if err != nil {
				tui.Fatal("project data directory: %v", err)
			}

			certDir := filepath.Join(dataDir, "certs")
			cert := filepath.Join(certDir, "server.crt")
			key := filepath.Join(certDir, "server.key")
			ca := filepath.Join(certDir, "ca.crt")
			for _, path := range []string{cert, key, ca} {
				if _, err := os.Stat(path); err != nil {
					tui.Fatal("missing %s, run \"rklings setup\" first", path)
				}
			}

			switch ex.ServeKind {
			case "operator":
				serverPath, err := build.BuildServer(buildExercise, cat)
				if err != nil {
					tui.Fatal("build operator: %v", err)
				}
				process, err := operator.Serve(cmd.Context(), serverPath, root, certDir, listenAddr, webAddr)
				if err != nil {
					tui.Fatal("serve %s: %v", ex.ID, err)
				}
				defer process.Close()
				if err := process.Wait(); err != nil && cmd.Context().Err() == nil {
					tui.Fatal("operator: %v", err)
				}
			case "lab":
				server, err := labserver.Start(labserver.Config{
					ExerciseID: ex.ID, Title: ex.Title, Code: ex.Code,
					Commands: ex.Commands, Transfer: ex.Transfer,
					HandshakeOnly: ex.HandshakeOnly, Welcome: ex.Welcome,
					TLSAddr: listenAddr, HTTPAddr: webAddr,
					CertFile: cert, KeyFile: key, CAFile: ca,
				})
				if err != nil {
					tui.Fatal("serve %s: %v", ex.ID, err)
				}
				defer server.Close()
				tui.Pass("%s lab: http://%s", ex.ID, webAddr)
				<-cmd.Context().Done()
			case "standalone":
				serverPath, err := build.BuildServer(buildExercise, cat)
				if err != nil {
					tui.Fatal("build server: %v", err)
				}
				if serverPath == "" {
					tui.Fatal("%s did not produce a server", ex.ID)
				}
				server := exec.CommandContext(cmd.Context(), serverPath, cert, key, ca)
				server.Env = append(os.Environ(), "RKLINGS_LISTEN_ADDR="+listenAddr)
				server.Stdout = os.Stdout
				server.Stderr = os.Stderr
				if err := server.Start(); err != nil {
					tui.Fatal("start %s server: %v", ex.ID, err)
				}
				publicAddr := listenAddr
				if strings.HasPrefix(publicAddr, ":") {
					publicAddr = "127.0.0.1" + publicAddr
				}
				tui.Pass("%s decoy: https://%s", ex.Code, publicAddr)
				if err := server.Wait(); err != nil && cmd.Context().Err() == nil {
					tui.Fatal("server: %v", err)
				}
			}
		},
	}
	c.Flags().StringVar(&listenAddr, "listen", operator.LabListenAddr, "agent mTLS listen address")
	c.Flags().StringVar(&webAddr, "web", operator.DashboardAddr, "browser listen address")
	c.Flags().BoolVar(&useRef, "reference", false, "serve the reference solution")
	return c
}
