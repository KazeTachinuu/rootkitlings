package state

import (
	"encoding/json"
	"os"
	"path/filepath"
	"time"
)

type Progress struct {
	Completed map[string]time.Time `json:"completed"`
}

func path() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".rklings", "progress.json")
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
