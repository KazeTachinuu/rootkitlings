package state

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/KazeTachinuu/rootkitlings/internal/project"
)

type Progress struct {
	Completed map[string]time.Time `json:"completed"`
}

type Lab struct {
	Name     string    `json:"name"`
	Exercise string    `json:"exercise"`
	SSHPort  int       `json:"ssh_port"`
	KeyPath  string    `json:"key_path"`
	OwnerPID int       `json:"owner_pid"`
	Started  time.Time `json:"started"`
}

func labDir() string {
	dataDir, err := project.DataDir()
	if err != nil {
		return filepath.Join(".rklings", "runs")
	}
	return filepath.Join(dataDir, "runs")
}

func labPath(ownerPID int) string {
	return filepath.Join(labDir(), strconv.Itoa(ownerPID)+".json")
}

func SaveLab(lab Lab) error {
	labs, err := LoadLabs()
	if err != nil {
		return err
	}
	for _, running := range labs {
		if running.Name == lab.Name && running.OwnerPID != lab.OwnerPID {
			return fmt.Errorf("lab name %q is already running", lab.Name)
		}
	}
	path := labPath(lab.OwnerPID)
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	data, err := json.MarshalIndent(lab, "", "  ")
	if err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".run-*.json")
	if err != nil {
		return err
	}
	name := temporary.Name()
	defer os.Remove(name)
	if err := temporary.Chmod(0600); err != nil {
		temporary.Close()
		return err
	}
	if _, err := temporary.Write(data); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	return os.Rename(name, path)
}

func LoadLabs() ([]Lab, error) {
	entries, err := os.ReadDir(labDir())
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	var labs []Lab
	for _, entry := range entries {
		if entry.IsDir() || filepath.Ext(entry.Name()) != ".json" {
			continue
		}
		path := filepath.Join(labDir(), entry.Name())
		data, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		var lab Lab
		if json.Unmarshal(data, &lab) != nil || lab.Name == "" || lab.SSHPort < 1 ||
			lab.SSHPort > 65535 || lab.KeyPath == "" || lab.OwnerPID < 1 {
			_ = os.Remove(path)
			continue
		}
		if syscall.Kill(lab.OwnerPID, 0) != nil {
			_ = os.Remove(path)
			continue
		}
		labs = append(labs, lab)
	}
	sort.Slice(labs, func(i, j int) bool { return labs[i].Name < labs[j].Name })
	return labs, nil
}

func FindLab(name string) (*Lab, error) {
	labs, err := LoadLabs()
	if err != nil {
		return nil, err
	}
	if name == "" && len(labs) == 1 {
		return &labs[0], nil
	}
	for i := range labs {
		if labs[i].Name == name {
			return &labs[i], nil
		}
	}
	if len(labs) == 0 {
		return nil, fmt.Errorf("no running lab")
	}
	if name == "" {
		names := make([]string, len(labs))
		for i := range labs {
			names[i] = labs[i].Name
		}
		return nil, fmt.Errorf("multiple labs are running; choose one: %s", strings.Join(names, ", "))
	}
	return nil, fmt.Errorf("lab %q is not running", name)
}

func ClearLab(ownerPID int) error {
	err := os.Remove(labPath(ownerPID))
	if os.IsNotExist(err) {
		return nil
	}
	return err
}

func path() string {
	dataDir, err := project.DataDir()
	if err != nil {
		return filepath.Join(".rklings", "progress.json")
	}
	return filepath.Join(dataDir, "progress.json")
}

func Load() *Progress {
	p := &Progress{Completed: make(map[string]time.Time)}
	data, err := os.ReadFile(path())
	if err != nil {
		return p
	}
	json.Unmarshal(data, p)
	if p.Completed == nil {
		p.Completed = make(map[string]time.Time)
	}
	return p
}

func (p *Progress) MarkDone(exerciseID string) error {
	p.Completed[exerciseID] = time.Now()
	return p.save()
}

func (p *Progress) Clear(exerciseID string) error {
	delete(p.Completed, exerciseID)
	return p.save()
}

func (p *Progress) IsDone(exerciseID string) bool {
	_, ok := p.Completed[exerciseID]
	return ok
}

func (p *Progress) save() error {
	dir := filepath.Dir(path())
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	data, err := json.MarshalIndent(p, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path(), data, 0600)
}
