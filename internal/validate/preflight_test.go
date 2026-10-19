package validate

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
)

func TestUnfinishedTODOsScansOnlyEditableFiles(t *testing.T) {
	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, "main.c"),
		[]byte("ok\n// TODO: implement control\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "README.md"),
		[]byte("TODO: documentation example\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	ex := &catalog.Exercise{Source: root, EditableFiles: []string{"main.c"}}
	got, err := unfinishedTODOs(root, ex)
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 || got[0].file != "main.c" || got[0].line != 2 ||
		got[0].column != 4 || got[0].text != "implement control" {
		t.Fatalf("unfinishedTODOs() = %#v", got)
	}
}

func TestUnfinishedErrorUsesCompilerDiagnosticFormat(t *testing.T) {
	err := unfinishedError([]todoLocation{{
		file: "exercises/L04/interface/proc.c", line: 27, column: 5,
		text: "implement control",
	}})
	want := "exercises/L04/interface/proc.c:27:5: implement control"
	if !strings.Contains(err.Error(), want) {
		t.Fatalf("unfinishedError() = %q, want line %q", err, want)
	}
	if strings.Contains(err.Error(), ": TODO:") {
		t.Fatalf("unfinishedError() repeats TODO marker: %q", err)
	}
}
