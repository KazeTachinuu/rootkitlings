package validate

import (
	"path/filepath"
	"testing"

	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
)

func TestShouldMarkProgress(t *testing.T) {
	t.Run("learner validation", func(t *testing.T) {
		t.Setenv("RKLINGS_NO_PROGRESS", "")
		if !shouldMarkProgress(&catalog.Exercise{}) {
			t.Fatal("learner validation should mark progress")
		}
	})

	t.Run("reference validation", func(t *testing.T) {
		t.Setenv("RKLINGS_NO_PROGRESS", "")
		if shouldMarkProgress(&catalog.Exercise{Reference: true}) {
			t.Fatal("reference validation must not mark progress")
		}
	})

	t.Run("explicitly disabled", func(t *testing.T) {
		t.Setenv("RKLINGS_NO_PROGRESS", "1")
		if shouldMarkProgress(&catalog.Exercise{}) {
			t.Fatal("disabled progress must remain unchanged")
		}
	})
}

func TestIsCaseArtifactDir(t *testing.T) {
	dataDir := filepath.Join(t.TempDir(), ".rklings")
	valid := filepath.Join(dataDir, "tmp", "build-case-123", "rklings.ko")
	if !isCaseArtifactDir(dataDir, valid) {
		t.Fatal("generated validation artifact should be removable")
	}
	for _, unsafe := range []string{
		filepath.Join(dataDir, "tmp", "other", "rklings.ko"),
		filepath.Join(dataDir, "build-case-123", "rklings.ko"),
		filepath.Join(filepath.Dir(dataDir), "tmp", "build-case-123", "rklings.ko"),
	} {
		if isCaseArtifactDir(dataDir, unsafe) {
			t.Fatalf("unsafe artifact accepted: %s", unsafe)
		}
	}
}
