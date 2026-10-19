package catalog

import (
	"path/filepath"
	"runtime"
	"testing"
)

func repositoryRoot(t *testing.T) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("resolve test path")
	}
	return filepath.Clean(filepath.Join(filepath.Dir(file), "..", ".."))
}

func TestCourseCatalog(t *testing.T) {
	cat, err := Load(repositoryRoot(t))
	if err != nil {
		t.Fatal(err)
	}
	if got, want := len(cat.Exercises), 31; got != want {
		t.Fatalf("exercise count = %d, want %d", got, want)
	}
	seen := make(map[string]bool)
	for _, exercise := range cat.Exercises {
		for _, prerequisite := range exercise.Prerequisites {
			if !seen[prerequisite] {
				t.Fatalf("%s appears before prerequisite %s", exercise.ID, prerequisite)
			}
		}
		seen[exercise.ID] = true
	}
}

func TestSpecialBuildsAreDeclarative(t *testing.T) {
	cat, err := Load(repositoryRoot(t))
	if err != nil {
		t.Fatal(err)
	}
	tests := map[string]func(*Exercise) bool{
		"fileless-module": func(ex *Exercise) bool { return ex.EmbedModuleFrom != "" },
		"fileless-exec":   func(ex *Exercise) bool { return ex.EmbedFixture != "" },
		"remote-control":  func(ex *Exercise) bool { return ex.ServeKind == "operator" && ex.ServerDir != "" },
		"decoy-routing":   func(ex *Exercise) bool { return ex.ServeKind == "standalone" },
		"complete-rootkit": func(ex *Exercise) bool {
			return ex.KernelDir != "" && ex.AgentDir != "" && ex.EmbedKernel && ex.AgentLDFlags != "" &&
				ex.ServeKind == "operator" && ex.ServerPath == "product/operator" &&
				len(ex.RunStage) == 1 && ex.RunStage[0] == "agent" &&
				len(ex.RunLoad) == 0 && ex.RunAgent == "manual"
		},
	}
	for id, valid := range tests {
		exercise := cat.Find(id)
		if exercise == nil || !valid(exercise) {
			t.Errorf("%s is missing declarative build metadata", id)
		}
	}
}

func TestFocusedVisualLabsAreL15ThroughL19(t *testing.T) {
	cat, err := Load(repositoryRoot(t))
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]bool{
		"mtls-connect": true, "mkdir-bridge": true, "command-dispatch": true,
		"remote-shell": true, "file-transfer": true,
	}
	for i := range cat.Exercises {
		exercise := &cat.Exercises[i]
		if exercise.ServeKind == "lab" && !want[exercise.ID] {
			t.Errorf("unexpected visual lab %s", exercise.ID)
		}
		if want[exercise.ID] && exercise.ServeKind != "lab" {
			t.Errorf("%s is not configured as a visual lab", exercise.ID)
		}
	}
}

func TestEveryExerciseDeclaresInteractiveRun(t *testing.T) {
	cat, err := Load(repositoryRoot(t))
	if err != nil {
		t.Fatal(err)
	}
	for _, exercise := range cat.Exercises {
		if len(exercise.RunStage) == 0 {
			t.Errorf("%s does not declare run.stage", exercise.ID)
		}
		if exercise.RunAgent == "" && exercise.Stages("agent") {
			t.Errorf("%s stages an agent without declaring run.agent", exercise.ID)
		}
	}
}

func TestRunAndValidationLifecycleAreIndependent(t *testing.T) {
	cat, err := Load(repositoryRoot(t))
	if err != nil {
		t.Fatal(err)
	}
	project := cat.Find("complete-rootkit")
	if project == nil {
		t.Fatal("complete-rootkit is missing")
	}
	if project.RunAgent != "manual" || len(project.RunLoad) != 0 {
		t.Fatalf("P00 run must stage without loading or starting: %#v", project)
	}
	if !project.Harness || project.Runtime != "background" {
		t.Fatal("P00 validation must retain its independent background harness")
	}
}
