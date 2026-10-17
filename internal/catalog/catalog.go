package catalog

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/BurntSushi/toml"
)

type Exercise struct {
	ID            string
	Title         string
	Dir           string // full path to exercise dir
	Kind          string // "lesson" or "milestone"
	Difficulty    string
	Prerequisites []string
	Source        string // workspace.source path
	Solution      string // resources.solution path
	Readme        string // resources.readme path
	Hint          string // resources.hint path
	Checks        []Check
	Timeout       int // verification.timeout_seconds
}

type Check struct {
	ID     string
	Label  string
	Script string // path to check script
}

type Catalog struct {
	Root      string
	Exercises []Exercise
	ByID      map[string]*Exercise
}

type courseToml struct {
	ExerciseRoots []string `toml:"exercise_roots"`
	RequiredRoute []string `toml:"required_route"`
}

type exerciseToml struct {
	ID            string   `toml:"id"`
	Title         string   `toml:"title"`
	Kind          string   `toml:"kind"`
	Difficulty    string   `toml:"difficulty"`
	Prerequisites []string `toml:"prerequisites"`
	Workspace     struct {
		Source string `toml:"source"`
	} `toml:"workspace"`
	Resources struct {
		Readme   string `toml:"readme"`
		Hint     string `toml:"hint"`
		Solution string `toml:"solution"`
	} `toml:"resources"`
	Verification struct {
		Timeout int `toml:"timeout_seconds"`
		Cases   []struct {
			ID    string `toml:"id"`
			Label string `toml:"label"`
			Check string `toml:"check"`
		} `toml:"cases"`
	} `toml:"verification"`
}

func Load(root string) (*Catalog, error) {
	coursePath := filepath.Join(root, "course.toml")
	var course courseToml
	if _, err := toml.DecodeFile(coursePath, &course); err != nil {
		return nil, fmt.Errorf("course.toml: %w", err)
	}

	cat := &Catalog{
		Root: root,
		ByID: make(map[string]*Exercise),
	}

	found := make(map[string]*Exercise)
	for _, exRoot := range course.ExerciseRoots {
		absRoot := filepath.Join(root, exRoot)
		entries, err := os.ReadDir(absRoot)
		if err != nil {
			return nil, fmt.Errorf("exercise root %s: %w", exRoot, err)
		}
		for _, e := range entries {
			if !e.IsDir() {
				continue
			}
			tomlPath := filepath.Join(absRoot, e.Name(), "exercise.toml")
			if _, err := os.Stat(tomlPath); err != nil {
				continue
			}
			var et exerciseToml
			if _, err := toml.DecodeFile(tomlPath, &et); err != nil {
				return nil, fmt.Errorf("%s: %w", tomlPath, err)
			}
			ex := &Exercise{
				ID:            et.ID,
				Title:         et.Title,
				Dir:           filepath.Join(absRoot, e.Name()),
				Kind:          et.Kind,
				Difficulty:    et.Difficulty,
				Prerequisites: et.Prerequisites,
				Source:        filepath.Join(root, et.Workspace.Source),
				Solution:      filepath.Join(root, et.Resources.Solution),
				Readme:        filepath.Join(root, et.Resources.Readme),
				Hint:          filepath.Join(root, et.Resources.Hint),
				Timeout:       et.Verification.Timeout,
			}
			for _, c := range et.Verification.Cases {
				ex.Checks = append(ex.Checks, Check{
					ID:     c.ID,
					Label:  c.Label,
					Script: filepath.Join(root, c.Check),
				})
			}
			found[et.ID] = ex
		}
	}

	// Order by required_route, skip missing
	for _, id := range course.RequiredRoute {
		if ex, ok := found[id]; ok {
			cat.Exercises = append(cat.Exercises, *ex)
			cat.ByID[id] = &cat.Exercises[len(cat.Exercises)-1]
		}
	}

	return cat, nil
}

func (c *Catalog) Find(name string) *Exercise {
	if ex, ok := c.ByID[name]; ok {
		return ex
	}
	// Try matching by directory prefix (e.g., "L00" or "l00")
	for i := range c.Exercises {
		base := filepath.Base(c.Exercises[i].Dir)
		if len(name) >= 3 && len(base) >= 3 && equalFold(base[:3], name[:min(3, len(name))]) {
			return &c.Exercises[i]
		}
	}
	return nil
}

func equalFold(a, b string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range len(a) {
		ca, cb := a[i], b[i]
		if ca >= 'A' && ca <= 'Z' {
			ca += 'a' - 'A'
		}
		if cb >= 'A' && cb <= 'Z' {
			cb += 'a' - 'A'
		}
		if ca != cb {
			return false
		}
	}
	return true
}
