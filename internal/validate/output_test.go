package validate

import (
	"reflect"
	"testing"
)

func TestDiagnosticLinesKeepsOnlyActionableFailures(t *testing.T) {
	output := `[+] module loaded
/vm/check: 40: printf: I/O error
[-] expected active=0, got active=1
[+] kernel log is clean
Results 28 passed, 2 failed`
	want := []string{
		"/vm/check: 40: printf: I/O error",
		"[-] expected active=0, got active=1",
		"Results 28 passed, 2 failed",
	}
	if got := diagnosticLines(output); !reflect.DeepEqual(got, want) {
		t.Fatalf("diagnosticLines() = %#v, want %#v", got, want)
	}
}

func TestDiagnosticLinesProvidesFallback(t *testing.T) {
	want := []string{"check failed without diagnostic output"}
	if got := diagnosticLines("ordinary output"); !reflect.DeepEqual(got, want) {
		t.Fatalf("diagnosticLines() = %#v, want %#v", got, want)
	}
}
