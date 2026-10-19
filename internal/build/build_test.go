package build

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
)

func TestEmbeddedToolchains(t *testing.T) {
	for _, id := range []string{"course", "deployment"} {
		tc, err := loadToolchain(id)
		if err != nil {
			t.Fatalf("load %s toolchain: %v", id, err)
		}
		if tc.Kernel == "" || tc.Image == "" {
			t.Fatalf("%s toolchain is incomplete: %#v", id, tc)
		}
		if !strings.Contains(tc.Base, "@sha256:") {
			t.Fatalf("%s base image is not digest-pinned: %q", id, tc.Base)
		}
	}
}

func TestCapturedArgumentsRemovesBuildOutputs(t *testing.T) {
	output := `gcc -I/usr/src/linux/include -Wp,-MMD,/src/.main.o.d -c -o /src/main.o /src/main.c`
	arguments, err := capturedArguments(output)
	if err != nil {
		t.Fatal(err)
	}
	joined := strings.Join(arguments, " ")
	if strings.Contains(joined, "/src/main") || strings.Contains(joined, "-MMD") {
		t.Fatalf("build-only arguments remain: %v", arguments)
	}
}

func TestCourseCSourcesIncludesWorkspaceAndSolutionOnly(t *testing.T) {
	root := t.TempDir()
	for _, name := range []string{
		"exercises/L00/main.c",
		"exercises/L00/.rklings/solution/core/main.c",
		"exercises/L00/.rklings/checks/helper.c",
	} {
		path := filepath.Join(root, name)
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, nil, 0644); err != nil {
			t.Fatal(err)
		}
	}
	sources, err := courseCSources(root)
	if err != nil {
		t.Fatal(err)
	}
	if len(sources) != 2 {
		t.Fatalf("got %d sources, want 2: %v", len(sources), sources)
	}
}

func TestStageWorkspaceCopiesOnlyDeclaredFiles(t *testing.T) {
	source := t.TempDir()
	destination := t.TempDir()
	for name, content := range map[string]string{
		"core/main.c":                "learner",
		".rklings/solution/main.c":   "solution",
		".rklings/checks/validation": "check",
	} {
		path := filepath.Join(source, name)
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	exercise := &catalog.Exercise{
		Source:         source,
		WorkspaceFiles: []string{"core/main.c"},
	}
	if err := stageWorkspace(destination, exercise); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(destination, "core", "main.c")); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(destination, ".rklings")); !os.IsNotExist(err) {
		t.Fatalf("hidden internals leaked into build stage: %v", err)
	}
}

func TestFilesBelowRebasesNestedWorkspace(t *testing.T) {
	got := filesBelow([]string{"kernel/Kbuild", "agent/main.go", "README.md"}, "agent")
	if len(got) != 1 || got[0] != "main.go" {
		t.Fatalf("filesBelow() = %v, want [main.go]", got)
	}
}

func TestEmbeddedContainerfileUsesDeclaredInputs(t *testing.T) {
	data, err := containerAssets.ReadFile("container/Containerfile")
	if err != nil {
		t.Fatal(err)
	}
	content := string(data)
	for _, input := range []string{"ARG BASE", "ARG KERNEL", "ARG SOURCES"} {
		if !strings.Contains(content, input) {
			t.Errorf("Containerfile is missing %q", input)
		}
	}
}

func TestPublishArtifactReplacesAtomically(t *testing.T) {
	dir := t.TempDir()
	source := filepath.Join(dir, "source")
	destination := filepath.Join(dir, "artifact")
	if err := os.WriteFile(source, []byte("complete"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(destination, []byte("old"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := publishArtifact(source, destination); err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(destination)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != "complete" {
		t.Fatalf("published %q, want complete", got)
	}
}
