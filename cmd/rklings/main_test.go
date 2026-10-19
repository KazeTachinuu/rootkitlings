package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
	"github.com/KazeTachinuu/rootkitlings/internal/operator"
)

func TestValidSSHUser(t *testing.T) {
	for _, user := range []string{"student", "root", "lab-user"} {
		if !validSSHUser(user) {
			t.Errorf("validSSHUser(%q) = false", user)
		}
	}
	for _, user := range []string{"", "student@host", "student root", "../root"} {
		if validSSHUser(user) {
			t.Errorf("validSSHUser(%q) = true", user)
		}
	}
}

func TestResetWorkspaceRestoresOnlyLearnerFiles(t *testing.T) {
	root := t.TempDir()
	exerciseDir := filepath.Join(root, "exercises", "L00")
	learner := filepath.Join(exerciseDir, "main.c")
	internal := filepath.Join(exerciseDir, ".rklings", "solution", "main.c")
	for path, content := range map[string]string{
		learner:  "learner\n",
		internal: "reference\n",
	} {
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	runGit(t, root, "init", "-q")
	runGit(t, root, "add", ".")
	runGit(t, root, "-c", "user.name=Rootkitlings", "-c",
		"user.email=test@invalid", "commit", "-qm", "fixture")
	if err := os.WriteFile(learner, []byte("changed\n"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(internal, []byte("keep\n"), 0644); err != nil {
		t.Fatal(err)
	}
	exercise := &catalog.Exercise{
		Source:         exerciseDir,
		WorkspaceFiles: []string{"main.c"},
	}
	if err := resetWorkspace(root, exercise); err != nil {
		t.Fatal(err)
	}
	assertContent(t, learner, "learner\n")
	assertContent(t, internal, "keep\n")
}

func TestSolutionFilesUsesReferenceLayout(t *testing.T) {
	root := t.TempDir()
	for _, name := range []string{"Kbuild", "core/init.c", "go.sum"} {
		path := filepath.Join(root, name)
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, nil, 0644); err != nil {
			t.Fatal(err)
		}
	}
	files, err := solutionFiles(root)
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.Join(files, ","); got != "Kbuild,core/init.c" {
		t.Fatalf("solutionFiles() = %q", got)
	}
}

func runGit(t *testing.T, directory string, args ...string) {
	t.Helper()
	command := exec.Command("git", args...)
	command.Dir = directory
	if output, err := command.CombinedOutput(); err != nil {
		t.Fatalf("git %v: %v: %s", args, err, output)
	}
}

func assertContent(t *testing.T, path, want string) {
	t.Helper()
	content, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if string(content) != want {
		t.Fatalf("%s = %q, want %q", path, content, want)
	}
}

func TestServeDefaultsToRootlessAgentPort(t *testing.T) {
	command := serveCmd()
	flag := command.Flags().Lookup("listen")
	if flag == nil {
		t.Fatal("server command has no --listen flag")
	}
	if flag.DefValue != operator.LabListenAddr {
		t.Fatalf("--listen default = %q, want %q", flag.DefValue, operator.LabListenAddr)
	}
	if command.Flags().Lookup("reference") == nil {
		t.Fatal("server command has no --reference flag")
	}
}
