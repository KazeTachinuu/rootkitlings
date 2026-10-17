package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/hugo/rootkitlings/internal/build"
	"github.com/hugo/rootkitlings/internal/catalog"
	isetup "github.com/hugo/rootkitlings/internal/setup"
	"github.com/hugo/rootkitlings/internal/state"
	"github.com/hugo/rootkitlings/internal/tui"
	"github.com/hugo/rootkitlings/internal/validate"
	"github.com/hugo/rootkitlings/internal/vm"
	"github.com/spf13/cobra"
)

func main() {
	root := &cobra.Command{
		Use:   "rklings",
		Short: "Educational rootkit engineering course",
	}

	root.AddCommand(
		setupCmd(),
		buildCmd(),
		runCmd(),
		validateCmd(),
		statusCmd(),
		solutionCmd(),
		resetCmd(),
		serverCmd(),
	)

	if err := root.Execute(); err != nil {
		os.Exit(1)
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
	return ""
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
	var useRef bool
	c := &cobra.Command{
		Use:   "build <exercise>",
		Short: "Build exercise kernel module",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])
			if useRef {
				if ex.Solution == "" {
					tui.Fatal("no reference solution for %s", ex.ID)
				}
				ex = ex.WithSource(ex.Solution)
			}
			koPath, err := build.Build(ex)
			if err != nil {
				tui.Fatal("%v", err)
			}
			tui.Pass("%s", koPath)
		},
	}
	c.Flags().BoolVar(&useRef, "reference", false, "build against the reference solution")
	return c
}

func runCmd() *cobra.Command {
	var useRef bool
	c := &cobra.Command{
		Use:   "run <exercise>",
		Short: "Build, boot VM, and open interactive shell",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])

			if useRef {
				if ex.Solution == "" {
					tui.Fatal("no reference solution for %s", ex.ID)
				}
				ex = ex.WithSource(ex.Solution)
			}

			// D00 (deployment) has no .ko to build.
			var koPath string
			if ex.Kind != "deployment" {
				var err error
				koPath, err = build.Build(ex)
				if err != nil {
					tui.Fatal("build: %v", err)
				}
			}

			home, err := os.UserHomeDir()
			if err != nil {
				tui.Fatal("home dir: %v", err)
			}

			// D00 uses the 24.04 image; everything else uses the 26.04 image.
			imageName := isetup.VMImageName
			if ex.Kind == "deployment" {
				imageName = isetup.D00ImageName
			}
			imagePath := filepath.Join(home, ".rklings", "cache", imageName)

			tmpDir, err := os.MkdirTemp("", "rklings-run-")
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

			// D00 forwards port 8080 for the vulnerable service.
			var extraFwd []string
			if ex.Kind == "deployment" {
				extraFwd = []string{"tcp::8080-:8080"}
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
			})
			if err != nil {
				tui.Fatal("VM boot: %v", err)
			}
			defer v.Close()
			tui.Pass("VM ready (%.1fs)", v.BootTime().Seconds())

			if koPath != "" {
				v.SSH("mkdir -p /rklings")
				if err := v.SCP(koPath, "/rklings/rklings.ko"); err != nil {
					tui.Fatal("SCP: %v", err)
				}
				if ex.PreloadModule {
					if out, err := v.SSH("insmod /rklings/rklings.ko"); err != nil {
						tui.Warn("insmod: %s", out)
					} else {
						tui.Pass("module loaded")
					}
				}
			}

			// Switch to the exercise user for the interactive shell.
			v.SetUser(vmUser)

			if err := v.Shell(); err != nil {
				tui.Warn("shell exited: %v", err)
			}
		},
	}
	c.Flags().BoolVar(&useRef, "reference", false, "build against the reference solution")
	return c
}

func validateCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "validate <exercise>",
		Short: "Validate exercise solution",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, root := loadCatalog()
			ex := findExercise(cat, args[0])
			if err := validate.Run(root, ex, cat); err != nil {
				tui.Fatal("%v", err)
			}
		},
	}
}

func statusCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "status",
		Short: "Show exercise progress",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			prog := state.Load()
			for _, ex := range cat.Exercises {
				if prog.IsDone(ex.ID) {
					tui.Pass("%s: %s", ex.ID, ex.Title)
				} else {
					tui.Pending("%s: %s", ex.ID, ex.Title)
				}
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

			filepath.Walk(ex.Solution, func(path string, info os.FileInfo, err error) error {
				if err != nil || info.IsDir() {
					return nil
				}
				rel, _ := filepath.Rel(ex.Solution, path)
				fmt.Printf("--- %s ---\n", rel)
				data, err := os.ReadFile(path)
				if err != nil {
					tui.Warn("read %s: %v", rel, err)
					return nil
				}
				fmt.Print(string(data))
				if len(data) > 0 && data[len(data)-1] != '\n' {
					fmt.Println()
				}
				return nil
			})
		},
	}
}

func resetCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "reset <exercise>",
		Short: "Reset exercise to starter state",
		Args:  cobra.ExactArgs(1),
		Run: func(cmd *cobra.Command, args []string) {
			cat, _ := loadCatalog()
			ex := findExercise(cat, args[0])

			gitCmd := exec.Command("git", "checkout", "--", ex.Source)
			gitCmd.Stdout = os.Stdout
			gitCmd.Stderr = os.Stderr
			if err := gitCmd.Run(); err != nil {
				tui.Fatal("git checkout: %v", err)
			}

			prog := state.Load()
			if err := prog.Clear(ex.ID); err != nil {
				tui.Warn("clear progress: %v", err)
			}

			tui.Pass("reset %s", ex.ID)
		},
	}
}

func serverCmd() *cobra.Command {
	return &cobra.Command{
		Use:   "server",
		Short: "Start operator server",
		Args:  cobra.NoArgs,
		Run: func(cmd *cobra.Command, args []string) {
			tui.Info("server not yet implemented")
		},
	}
}
