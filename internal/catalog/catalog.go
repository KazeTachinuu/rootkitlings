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
	Kind          string // "lesson", "milestone", or "final-project"
	Stage         string // e.g. "module-core", "escalation", "concealment"
	Difficulty    string
	Prerequisites []string
	Tradecraft    []string // e.g. ["privilege", "concealment"]
	VMUser        string   // "root" or "student"
	PreloadModule bool     // insmod before giving shell
	Source        string   // workspace.source path
	Solution      string   // resources.solution path
	Readme        string   // resources.readme path
	Hint          string   // resources.hint path
	Checks        []Check
	Tools         []string // verification.tools (e.g. "module-load-test")
	Timeout       int      // verification.timeout_seconds
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
	SchemaVersion int      `toml:"schema_version"`
	ID            string   `toml:"id"`
	Title         string   `toml:"title"`
	Stage         string   `toml:"stage"`
	Kind          string   `toml:"kind"`
	Tradecraft    []string `toml:"tradecraft"`
	Difficulty    string   `toml:"difficulty"`
	Prerequisites []string `toml:"prerequisites"`
	VMUser        string   `toml:"vm_user"`
	PreloadModule bool     `toml:"preload_module"`
	Workspace     struct {
		Source string `toml:"source"`
	} `toml:"workspace"`
	Resources struct {
		Readme   string `toml:"readme"`
		Hint     string `toml:"hint"`
		Solution string `toml:"solution"`
	} `toml:"resources"`
	Verification struct {
		Timeout int      `toml:"timeout_seconds"`
		Tools   []string `toml:"tools"`
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
			// Apply defaults for fields that may be absent from the toml.
			vmUser := et.VMUser
			if vmUser == "" {
				vmUser = "root"
			}

			ex := &Exercise{
				ID:            et.ID,
				Title:         et.Title,
				Dir:           filepath.Join(absRoot, e.Name()),
				Kind:          et.Kind,
				Stage:         et.Stage,
				Difficulty:    et.Difficulty,
				Prerequisites: et.Prerequisites,
				Tradecraft:    et.Tradecraft,
				VMUser:        vmUser,
				PreloadModule: et.PreloadModule,
				Source:        filepath.Join(root, et.Workspace.Source),
				Solution:      filepath.Join(root, et.Resources.Solution),
				Readme:        filepath.Join(root, et.Resources.Readme),
				Hint:          filepath.Join(root, et.Resources.Hint),
				Tools:         et.Verification.Tools,
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
	// Try matching by directory prefix (e.g., "L00" matches "L00-module-build-lifecycle").
	// The directory name format is "<PREFIX>-<slug>", so we check whether the
	// base name starts with <name> followed by a hyphen (case-insensitive).
	for i := range c.Exercises {
		base := filepath.Base(c.Exercises[i].Dir)
		if len(name) > 0 && len(base) > len(name) && base[len(name)] == '-' && equalFold(base[:len(name)], name) {
			return &c.Exercises[i]
		}
	}
	return nil
}

func (e *Exercise) WithSource(src string) *Exercise {
	copy := *e
	copy.Source = src
	return &copy
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
