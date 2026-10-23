package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
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
		infoCmd(),
		setupCmd(),
		compileDBCmd(),
		buildCmd(),
		runCmd(),
		sshCmd(),
		serveCmd(),
		validateCmd(),
		completeCmd(),
		solutionCmd(),
		resetCmd(),
	)

	if err := root.ExecuteContext(ctx); err != nil {
		os.Exit(1)
	}
}

func infoCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "info <exercise>",
		Short: "Show the executable learning contract for an exercise",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])
			out := cmd.OutOrStdout()
			fmt.Fprintf(out, "%s: %s\n", ex.Code, ex.Title)
			var prerequisites []string
			for _, prerequisite := range ex.Prerequisites {
				prior := cat.Find(prerequisite)
				if prior == nil {
					prerequisites = append(prerequisites, prerequisite)
				} else {
					prerequisites = append(prerequisites,
						fmt.Sprintf("%s — %s", prior.Code, prior.Title))
				}
			}
			printContractList(out, "Prerequisite", prerequisites)
			mechanisms := make([]string, 0, len(ex.Teaches))
			for _, concept := range ex.Teaches {
				label := cat.ConceptLabels[concept]
				if label == "" {
					label = readableConcept(concept)
				}
				mechanisms = append(mechanisms, label)
			}
			printContractList(out, "New mechanisms", mechanisms)
			editable := make(map[string]bool, len(ex.EditableFiles))
			for _, file := range ex.EditableFiles {
				editable[filepath.Clean(file)] = true
			}
			supplied := append([]string(nil), ex.Supplied...)
			for _, file := range ex.WorkspaceFiles {
				if !editable[filepath.Clean(file)] {
					supplied = append(supplied, file)
				}
			}
			printContractList(out, "Edit", ex.EditableFiles)
			printContractList(out, "Supplied", supplied)
			var proofs []string
			for _, check := range ex.Checks {
				proofs = append(proofs, check.Label)
			}
			printContractList(out, "Proof", proofs)
			if len(ex.CompletionProofs) != 0 {
				printContractList(out, "Completion proof", ex.CompletionProofs)
			}
		},
	}
}

func readableConcept(concept string) string {
	special := map[string]string{
		"bufio-scanner":       "bufio.Scanner",
		"call-usermodehelper": "call_usermodehelper",
		"exec-command":        "exec.Command",
		"finit-module":        "finit_module",
		"go-embed":            "Go embed",
		"io-limitreader":      "I/O LimitReader",
		"memfd-create":        "memfd_create",
		"os-mkdir-dispatch":   "OS mkdir Dispatch",
		"prctl-set-name":      "prctl Set Name",
		"proc-self-fd":        "/proc/self/fd",
	}
	if label := special[concept]; label != "" {
		return label
	}
	words := strings.Split(concept, "-")
	acronyms := map[string]string{
		"bufio": "bufio", "exec": "exec", "finit": "finit", "go": "Go",
		"aead": "AEAD", "io": "I/O", "memfd": "memfd", "os": "OS",
		"pid": "PID", "prctl": "prctl", "proc": "procfs", "pty": "PTY",
		"tcp": "TCP", "tls": "TLS", "utmp": "utmp/wtmp",
	}
	for i, word := range words {
		if replacement := acronyms[word]; replacement != "" {
			words[i] = replacement
			continue
		}
		words[i] = strings.ToUpper(word[:1]) + word[1:]
	}
	return strings.Join(words, " ")
}

func printContractList(out io.Writer, label string, values []string) {
	if len(values) == 0 {
		fmt.Fprintf(out, "%s: none\n", label)
		return
	}
	fmt.Fprintf(out, "%s:\n", label)
	for _, value := range values {
		fmt.Fprintf(out, "  - %s\n", value)
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
			output := cmd.OutOrStdout()
			out := tabwriter.NewWriter(output, 0, 4, 2, ' ', 0)
			progress := state.Load()
			completed := 0
			next := ""
			fmt.Fprintln(out, "CODE\tSTATUS\tTITLE")
			for _, exercise := range cat.Exercises {
				status := "locked"
				if progress.IsDone(exercise.ID) {
					status = "done"
					completed++
				} else if next == "" {
					status = "next"
					next = exercise.Code
				}
				fmt.Fprintf(out, "%s\t%s\t%s\n", exercise.Code, status, exercise.Title)
			}
			_ = out.Flush()
			fmt.Fprintf(output, "\nProgress: %d/%d\n", completed, len(cat.Exercises))
			if next == "" {
				fmt.Fprintln(output, "Course complete.")
			} else {
				fmt.Fprintf(output, "Next: ./rklings info %s\n", next)
			}
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
	return &cobra.Command{
		Use:   "setup",
		Short: "Check dependencies and initialize environment",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, args []string) {
			root := findRoot()
			if err := isetup.Run(root); err != nil {
				tui.Fatal("%v", err)
			}
		},
	}
}

func buildCmd() *cobra.Command {
	var useRef, force bool
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
			} else if !force {
				requirePrerequisites(ex)
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
	c.Flags().BoolVar(&force, "force", false, "skip prerequisite checks")
	return c
}

func requirePrerequisites(ex *catalog.Exercise) {
	progress := state.Load()
	var missing []string
	for _, prerequisite := range ex.Prerequisites {
		if !progress.IsDone(prerequisite) {
			missing = append(missing, prerequisite)
		}
	}
	if len(missing) != 0 {
		tui.Fatal("complete prerequisite(s) before %s: %s",
			ex.Code, strings.Join(missing, ", "))
	}
}

func runCmd() *cobra.Command {
	var useRef, force bool
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
			if !useRef && !force {
				requirePrerequisites(ex)
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
				if vmUser != "root" {
					if _, err := v.SSH("chown " + vmUser + ":" + vmUser + " " + artifactPath); err != nil {
						failRun("assign agent to %s: %v", vmUser, err)
					}
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
					if out, err := v.SSHAs(vmUser, env+artifactPath); err != nil {
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
					if out, err := v.SSHAs(vmUser, startAgent); err != nil {
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
					fmt.Printf("      RKLINGS_AGENT_ID=%s %s &\n", instanceName, stagedAgentPath)
				} else {
					tui.Info("the agent will connect when the host server is listening")
				}
				fmt.Println()
			}

			if ex.Kind == "deployment" {
				if out, err := v.SSHAs("root", "rm -f /home/student/.ssh/authorized_keys"); err != nil {
					failRun("seal deployment target: %v: %s", err, out)
				}
				tui.Pass("learner entry sealed; supported initial access is HTTP only")
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
	c.Flags().BoolVar(&force, "force", false, "skip prerequisite checks")
	c.Flags().StringVar(&instanceName, "name", "", "name this VM and operator target")
	return c
}

func completeCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "complete D00",
		Short: "Record a completed operational deployment lab",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])
			if ex.Kind != "deployment" {
				tui.Fatal("complete is only required for operational deployment labs")
			}
			lab, err := state.FindLab("")
			if err != nil || lab.Exercise != ex.Code {
				tui.Fatal("no active %s lab found", ex.Code)
			}
			sshBase := []string{"-i", lab.KeyPath, "-p", strconv.Itoa(lab.SSHPort),
				"-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
				"-o", "LogLevel=ERROR", "root@localhost"}
			run := func(script string) (string, error) {
				a := append(append([]string{}, sshBase...), script)
				out, err := exec.CommandContext(cmd.Context(), "ssh", a...).CombinedOutput()
				return strings.TrimSpace(string(out)), err
			}

			// Check module is loaded and responds to control interface
			before, err := run("dmesg | grep -c 'rklings: pong' || echo 0")
			if err != nil {
				tui.Fatal("cannot reach target")
			}
			if _, err := run("mkdir -p /tmp/rklings.ping"); err != nil {
				tui.Fatal("cannot send ping command")
			}
			after, err := run("dmesg | grep -c 'rklings: pong' || echo 0")
			if err != nil {
				tui.Fatal("cannot read dmesg")
			}
			if before == after {
				tui.Fatal("module did not respond to ping (is it loaded?)")
			}

			// Check module is hidden from lsmod
			lsmod, _ := run("lsmod | grep -c '^rklings ' || echo 0")
			if lsmod != "0" {
				tui.Fatal("module is visible in lsmod (not concealed)")
			}

			if err := state.Load().MarkDone(ex.ID); err != nil {
				tui.Fatal("save progress: %v", err)
			}
			tui.Pass("completed %s: module operational and concealed", ex.Code)
		},
	}
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
	var useRef, force bool
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
			if !useRef && !force {
				requirePrerequisites(ex)
			}
			if err := validate.RunCase(root, ex, cat, caseID); err != nil {
				tui.Fatal("%v", err)
			}
		},
	}
	c.Flags().BoolVar(&useRef, "reference", false, "validate the reference solution")
	c.Flags().BoolVar(&force, "force", false, "skip prerequisite checks")
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
	validatable := 0
	for _, ex := range cat.Exercises {
		if ex.Solution != "" {
			validatable++
		}
	}
	tui.Info("validating %d exercises with %d automatic worker(s)", validatable, workers)

	jobs := make(chan catalog.Exercise)
	results := make(chan validationResult)
	var group sync.WaitGroup
	for range workers {
		group.Add(1)
		go func() {
			defer group.Done()
			for exercise := range jobs {
				var output []byte
				var childErr error
				for attempt := range 2 {
					timeout := 10 * time.Minute
					childCtx, cancel := context.WithTimeout(ctx, timeout)
					child := exec.CommandContext(childCtx, executable, "validate", exercise.Code, "--reference")
					child.Dir = root
					child.Env = append(os.Environ(), "RKLINGS_NO_PROGRESS=1")
					output, childErr = child.CombinedOutput()
					cancel()
					if childErr == nil || attempt == 1 || !bytes.Contains(output, []byte("VM boot failed")) {
						break
					}
					tui.Warn("retrying %s (transient VM boot failure)", exercise.Code)
				}
				results <- validationResult{exercise: exercise, output: output, err: childErr}
			}
		}()
	}
	go func() {
		defer close(jobs)
		for _, exercise := range cat.Exercises {
			if exercise.Solution == "" {
				continue
			}
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
	// Validation guests each reserve 1 GiB and two vCPUs. Four concurrent
	// guests make long kernel and reboot checks intermittently lose SSH even
	// on otherwise capable hosts, turning a passing reference into a flaky
	// acceptance run. Two keeps useful parallelism without oversubscribing the
	// lab machinery.
	if workers > 2 {
		workers = 2
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

func solutionCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "solution <exercise>",
		Short: "Show the reference solution",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])

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

			keyDir := filepath.Join(dataDir, "keys")
			for _, name := range []string{"operator.key", "operator.pub", "agent.key", "agent.pub"} {
				if _, err := os.Stat(filepath.Join(keyDir, name)); err != nil {
					tui.Fatal("missing %s, run \"rklings setup\" first", filepath.Join(keyDir, name))
				}
			}

			switch ex.ServeKind {
			case "operator":
				serverPath, err := build.BuildServer(buildExercise, cat)
				if err != nil {
					tui.Fatal("build operator: %v", err)
				}
				process, err := operator.Serve(cmd.Context(), serverPath, root, keyDir, listenAddr, webAddr)
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
					Addr: listenAddr, HTTPAddr: webAddr,
					KeyDir: keyDir,
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
				server := exec.CommandContext(cmd.Context(), serverPath)
				server.Env = append(os.Environ(), "RKLINGS_LISTEN_ADDR="+listenAddr, "RKLINGS_PKI_DIR="+keyDir)
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
	c.Flags().StringVar(&listenAddr, "listen", operator.LabListenAddr, "agent listen address")
	c.Flags().StringVar(&webAddr, "web", operator.DashboardAddr, "browser listen address")
	c.Flags().BoolVar(&useRef, "reference", false, "serve the reference solution")
	return c
}
