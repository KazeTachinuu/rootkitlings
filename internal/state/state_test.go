package state

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestLabLifecycle(t *testing.T) {
	t.Setenv("RKLINGS_DATA_DIR", t.TempDir())
	lab := Lab{
		Name: "victim-1", Exercise: "P00", SSHPort: 2222,
		KeyPath:  filepath.Join(t.TempDir(), "id_ed25519"),
		OwnerPID: os.Getpid(), Started: time.Now(),
	}
	if err := SaveLab(lab); err != nil {
		t.Fatal(err)
	}
	loaded, err := FindLab("")
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Exercise != lab.Exercise || loaded.SSHPort != lab.SSHPort || loaded.OwnerPID != lab.OwnerPID {
		t.Fatalf("loaded lab = %#v", loaded)
	}
	if err := ClearLab(os.Getpid() + 1); err != nil {
		t.Fatal(err)
	}
	if _, err := FindLab(""); err != nil {
		t.Fatalf("another process cleared the lab: %v", err)
	}
	if err := ClearLab(os.Getpid()); err != nil {
		t.Fatal(err)
	}
	if _, err := FindLab(""); err == nil {
		t.Fatal("cleared lab still loads")
	}
}

func TestFindLabRequiresANameWhenSeveralRun(t *testing.T) {
	t.Setenv("RKLINGS_DATA_DIR", t.TempDir())
	key := filepath.Join(t.TempDir(), "id_ed25519")
	for _, lab := range []Lab{
		{Name: "victim-1", Exercise: "P00", SSHPort: 2201, KeyPath: key, OwnerPID: os.Getpid(), Started: time.Now()},
		{Name: "victim-2", Exercise: "P00", SSHPort: 2202, KeyPath: key, OwnerPID: os.Getppid(), Started: time.Now()},
	} {
		if err := SaveLab(lab); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := FindLab(""); err == nil {
		t.Fatal("unnamed selection succeeded with multiple running labs")
	}
	selected, err := FindLab("victim-2")
	if err != nil || selected.SSHPort != 2202 {
		t.Fatalf("selected = %#v, %v", selected, err)
	}
}
