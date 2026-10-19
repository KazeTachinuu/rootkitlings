package catalog

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/BurntSushi/toml"
)

type Exercise struct {
	ID                   string
	Title                string
	Dir                  string // full path to exercise dir
	Kind                 string // "lesson", "milestone", or "final-project"
	Stage                string // e.g. "module-core", "escalation", "concealment"
	Difficulty           string
	Prerequisites        []string
	Tradecraft           []string // e.g. ["privilege", "concealment"]
	VMUser               string   // "root" or "student"
	Source               string   // workspace.source path
	Solution             string   // resources.solution path
	Readme               string   // resources.readme path
	Checks               []Check
	Tools                []string // verification.tools (e.g. "module-load-test")
	Timeout              int      // verification.timeout_seconds
	Runtime              string   // verification.runtime: checks, once, or background
	ArtifactPath         string   // guest path for the built Go artifact
	RemoveArtifact       bool     // unlink the launcher after a successful background start
	PKI                  bool     // stage course PKI material for guest-side verification
	Harness              bool     // start the real host-side mTLS validation harness
	ValidationLoadKernel bool
	ValidationBaseCase   string
	ServeKind            string
	SourceFiles          []string
	KernelDir            string
	AgentDir             string
	ServerDir            string
	ServerPath           string
	EmbedModuleFrom      string
	EmbedKernel          bool
	EmbedFixture         string
	EmbedFixtureAs       string
	Commands             []string
	Transfer             bool
	HandshakeOnly        bool
	Welcome              string
	Code                 string
	WorkspaceFiles       []string
	EditableFiles        []string
	WorkspaceBuild       string
	Teaches              []string
	Requires             []string
	AgentLDFlags         string
	ModuleFrom           string
	RunStage             []string
	RunLoad              []string
	RunAgent             string
	RunArtifactPath      string
	RunRemove            bool
	RunPKI               bool
	Reference            bool
}

type Check struct {
	ID                  string
	Label               string
	Script              string // path to check script
	ExpectBuild         string
	BuildOutputContains string
	BuildVariables      map[string]string
	RebootAfter         bool
	Prepare             string
}

type Catalog struct {
	Root         string
	Exercises    []Exercise
	ByID         map[string]*Exercise
	ToolBinaries map[string]string // tool ID -> binary path relative to repo root
}

func (ex *Exercise) Stages(kind string) bool {
	if len(ex.RunStage) == 0 {
		return true
	}
	for _, artifact := range ex.RunStage {
		if artifact == kind {
			return true
		}
	}
	return false
}

func contains(values []string, value string) bool {
	for _, candidate := range values {
		if candidate == value {
			return true
		}
	}
	return false
}

func (ex *Exercise) Loads(kind string) bool { return contains(ex.RunLoad, kind) }

type courseToml struct {
	ExerciseRoots []string   `toml:"exercise_roots"`
	RequiredRoute []string   `toml:"required_route"`
	Tools         []toolToml `toml:"tools"`
}

type toolToml struct {
	ID     string `toml:"id"`
	Binary string `toml:"binary"`
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
	Teaches       []string `toml:"teaches"`
	Requires      []string `toml:"requires"`
	VMUser        string   `toml:"vm_user"`
	Workspace     struct {
		Files    []string `toml:"files"`
		Editable []string `toml:"editable"`
		Build    string   `toml:"build"`
	} `toml:"workspace"`
	Build struct {
		KernelDir       string `toml:"kernel_dir"`
		AgentDir        string `toml:"agent_dir"`
		ServerDir       string `toml:"server_dir"`
		ServerPath      string `toml:"server_path"`
		EmbedModuleFrom string `toml:"embed_module_from"`
		EmbedKernel     bool   `toml:"embed_kernel"`
		EmbedFixture    string `toml:"embed_fixture"`
		EmbedFixtureAs  string `toml:"embed_fixture_as"`
		AgentLDFlags    string `toml:"agent_ldflags"`
		ModuleFrom      string `toml:"module_from"`
	} `toml:"build"`
	Run struct {
		Stage        []string `toml:"stage"`
		Load         []string `toml:"load"`
		Agent        string   `toml:"agent"`
		ArtifactPath string   `toml:"artifact_path"`
		Remove       bool     `toml:"remove_artifact"`
		PKI          bool     `toml:"pki"`
	} `toml:"run"`
	Serve struct {
		Kind          string   `toml:"kind"`
		Commands      []string `toml:"commands"`
		Transfer      bool     `toml:"transfer"`
		HandshakeOnly bool     `toml:"handshake_only"`
		Welcome       string   `toml:"welcome"`
	} `toml:"serve"`
	Verification struct {
		Timeout        int      `toml:"timeout_seconds"`
		Tools          []string `toml:"tools"`
		Runtime        string   `toml:"runtime"`
		ArtifactPath   string   `toml:"artifact_path"`
		RemoveArtifact bool     `toml:"remove_artifact"`
		PKI            bool     `toml:"pki"`
		Harness        bool     `toml:"harness"`
		LoadKernel     bool     `toml:"load_kernel"`
		BaseCase       string   `toml:"base_case"`
		SourceFiles    []string `toml:"source_files"`
		Cases          []struct {
			ID                  string            `toml:"id"`
			Label               string            `toml:"label"`
			Check               string            `toml:"check"`
			ExpectBuild         string            `toml:"expect_build"`
			BuildOutputContains string            `toml:"build_output_contains"`
			BuildVariables      map[string]string `toml:"build_variables"`
			RebootAfter         bool              `toml:"reboot_after"`
			Prepare             string            `toml:"prepare"`
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
		Root:         root,
		ByID:         make(map[string]*Exercise),
		ToolBinaries: make(map[string]string),
	}
	for _, t := range course.Tools {
		cat.ToolBinaries[t.ID] = t.Binary
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
			tomlPath := filepath.Join(absRoot, e.Name(), ".rklings", "exercise.toml")
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

			exerciseDir := filepath.Join(absRoot, e.Name())
			ex := &Exercise{
				ID:                   et.ID,
				Title:                et.Title,
				Dir:                  exerciseDir,
				Kind:                 et.Kind,
				Stage:                et.Stage,
				Difficulty:           et.Difficulty,
				Prerequisites:        et.Prerequisites,
				Tradecraft:           et.Tradecraft,
				VMUser:               vmUser,
				Source:               exerciseDir,
				Solution:             filepath.Join(exerciseDir, ".rklings", "solution"),
				Readme:               filepath.Join(exerciseDir, "README.md"),
				Tools:                et.Verification.Tools,
				Timeout:              et.Verification.Timeout,
				Runtime:              et.Verification.Runtime,
				ArtifactPath:         et.Verification.ArtifactPath,
				RemoveArtifact:       et.Verification.RemoveArtifact,
				PKI:                  et.Verification.PKI,
				Harness:              et.Verification.Harness,
				ValidationLoadKernel: et.Verification.LoadKernel,
				ValidationBaseCase:   et.Verification.BaseCase,
				ServeKind:            et.Serve.Kind,
				SourceFiles:          et.Verification.SourceFiles,
				KernelDir:            et.Build.KernelDir,
				AgentDir:             et.Build.AgentDir,
				ServerDir:            et.Build.ServerDir,
				ServerPath:           et.Build.ServerPath,
				EmbedModuleFrom:      et.Build.EmbedModuleFrom,
				EmbedKernel:          et.Build.EmbedKernel,
				EmbedFixture:         et.Build.EmbedFixture,
				EmbedFixtureAs:       et.Build.EmbedFixtureAs,
				Commands:             et.Serve.Commands,
				Transfer:             et.Serve.Transfer,
				HandshakeOnly:        et.Serve.HandshakeOnly,
				Welcome:              et.Serve.Welcome,
				Code:                 strings.SplitN(e.Name(), "-", 2)[0],
				WorkspaceFiles:       et.Workspace.Files,
				EditableFiles:        et.Workspace.Editable,
				WorkspaceBuild:       et.Workspace.Build,
				Teaches:              et.Teaches,
				Requires:             et.Requires,
				AgentLDFlags:         et.Build.AgentLDFlags,
				ModuleFrom:           et.Build.ModuleFrom,
				RunStage:             et.Run.Stage,
				RunLoad:              et.Run.Load,
				RunAgent:             et.Run.Agent,
				RunArtifactPath:      et.Run.ArtifactPath,
				RunRemove:            et.Run.Remove,
				RunPKI:               et.Run.PKI,
			}
			for _, c := range et.Verification.Cases {
				checkPath := ""
				if c.Check != "" {
					checkPath = filepath.Join(root, c.Check)
				}
				ex.Checks = append(ex.Checks, Check{
					ID:                  c.ID,
					Label:               c.Label,
					Script:              checkPath,
					ExpectBuild:         c.ExpectBuild,
					BuildOutputContains: c.BuildOutputContains,
					BuildVariables:      c.BuildVariables,
					RebootAfter:         c.RebootAfter,
					Prepare:             c.Prepare,
				})
			}
			found[et.ID] = ex
		}
	}

	// Order by required_route, skip missing
	for _, id := range course.RequiredRoute {
		ex, ok := found[id]
		if !ok {
			return nil, fmt.Errorf("required exercise %q is missing", id)
		}
		if _, duplicate := cat.ByID[id]; duplicate {
			return nil, fmt.Errorf("required route contains duplicate %q", id)
		}
		cat.Exercises = append(cat.Exercises, *ex)
		cat.ByID[id] = &cat.Exercises[len(cat.Exercises)-1]
	}
	if len(found) != len(cat.Exercises) {
		return nil, fmt.Errorf("catalog has %d exercises but route contains %d", len(found), len(cat.Exercises))
	}
	for i := range cat.Exercises {
		if err := validateExercise(&cat.Exercises[i], cat.ToolBinaries); err != nil {
			return nil, err
		}
		for _, prerequisite := range cat.Exercises[i].Prerequisites {
			if cat.ByID[prerequisite] == nil {
				return nil, fmt.Errorf("%s: unknown prerequisite %q", cat.Exercises[i].ID, prerequisite)
			}
		}
		for _, dependency := range []string{cat.Exercises[i].ModuleFrom, cat.Exercises[i].EmbedModuleFrom} {
			if dependency != "" && cat.ByID[dependency] == nil {
				return nil, fmt.Errorf("%s: unknown module dependency %q", cat.Exercises[i].ID, dependency)
			}
		}
		if path := cat.Exercises[i].ServerPath; path != "" {
			clean := filepath.Clean(path)
			if filepath.IsAbs(path) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
				return nil, fmt.Errorf("%s: build.server_path escapes repository: %q", cat.Exercises[i].ID, path)
			}
			if _, err := os.Stat(filepath.Join(root, clean)); err != nil {
				return nil, fmt.Errorf("%s: build.server_path: %w", cat.Exercises[i].ID, err)
			}
		}
	}

	return cat, nil
}

func validateExercise(ex *Exercise, tools map[string]string) error {
	if len(ex.RunStage) == 0 {
		return fmt.Errorf("%s: run.stage must declare at least one artifact", ex.ID)
	}
	for field, artifacts := range map[string][]string{"stage": ex.RunStage, "load": ex.RunLoad} {
		seenArtifacts := make(map[string]bool, len(artifacts))
		for _, artifact := range artifacts {
			if artifact != "kernel" && artifact != "agent" {
				return fmt.Errorf("%s: invalid run.%s artifact %q", ex.ID, field, artifact)
			}
			if seenArtifacts[artifact] {
				return fmt.Errorf("%s: duplicate run.%s artifact %q", ex.ID, field, artifact)
			}
			seenArtifacts[artifact] = true
		}
	}
	if ex.RunAgent != "" && ex.RunAgent != "manual" && ex.RunAgent != "once" && ex.RunAgent != "background" {
		return fmt.Errorf("%s: invalid run.agent mode %q", ex.ID, ex.RunAgent)
	}
	if ex.RunAgent != "" && !ex.Stages("agent") {
		return fmt.Errorf("%s: run.agent requires agent in run.stage", ex.ID)
	}
	for _, artifact := range ex.RunLoad {
		if !ex.Stages(artifact) {
			return fmt.Errorf("%s: run.load artifact %q is not staged", ex.ID, artifact)
		}
	}
	for label, path := range map[string]string{"starter": ex.Source, "solution": ex.Solution, "README": ex.Readme} {
		if path == "" {
			return fmt.Errorf("%s: missing %s path", ex.ID, label)
		}
		if _, err := os.Stat(path); err != nil {
			return fmt.Errorf("%s: %s: %w", ex.ID, label, err)
		}
	}
	if ex.VMUser != "root" && ex.VMUser != "student" {
		return fmt.Errorf("%s: invalid vm_user %q", ex.ID, ex.VMUser)
	}
	if ex.Runtime != "" && ex.Runtime != "checks" && ex.Runtime != "once" && ex.Runtime != "background" {
		return fmt.Errorf("%s: invalid verification runtime %q", ex.ID, ex.Runtime)
	}
	if ex.ServeKind != "" && ex.ServeKind != "lab" && ex.ServeKind != "operator" && ex.ServeKind != "standalone" {
		return fmt.Errorf("%s: invalid serve kind %q", ex.ID, ex.ServeKind)
	}
	declared := make(map[string]bool, len(ex.WorkspaceFiles))
	for _, name := range ex.WorkspaceFiles {
		clean := filepath.Clean(name)
		if filepath.IsAbs(name) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
			return fmt.Errorf("%s: workspace file escapes source: %q", ex.ID, name)
		}
		declared[clean] = true
		if _, err := os.Stat(filepath.Join(ex.Source, clean)); err != nil {
			return fmt.Errorf("%s: declared workspace file %s: %w", ex.ID, name, err)
		}
	}
	for _, name := range ex.EditableFiles {
		if !declared[filepath.Clean(name)] {
			return fmt.Errorf("%s: editable file %q is not declared in workspace.files", ex.ID, name)
		}
	}
	if ex.WorkspaceBuild != "" {
		if _, err := os.Stat(filepath.Join(ex.Source, ex.WorkspaceBuild)); err != nil {
			return fmt.Errorf("%s: workspace build path: %w", ex.ID, err)
		}
	}
	for _, tool := range ex.Tools {
		if _, ok := tools[tool]; !ok {
			return fmt.Errorf("%s: unknown verification tool %q", ex.ID, tool)
		}
	}
	caseIDs := make(map[string]bool, len(ex.Checks))
	for _, check := range ex.Checks {
		if check.ID == "" {
			return fmt.Errorf("%s: verification case has no id", ex.ID)
		}
		if caseIDs[check.ID] {
			return fmt.Errorf("%s: duplicate verification case %q", ex.ID, check.ID)
		}
		caseIDs[check.ID] = true
		if check.Label == "" {
			return fmt.Errorf("%s/%s: verification case has no label", ex.ID, check.ID)
		}
		if check.ExpectBuild != "pass" && check.ExpectBuild != "fail" {
			return fmt.Errorf("%s/%s: expect_build must be pass or fail", ex.ID, check.ID)
		}
		if check.Script == "" {
			if check.ExpectBuild == "fail" {
				continue
			}
			return fmt.Errorf("%s/%s: missing check script", ex.ID, check.ID)
		}
		if _, err := os.Stat(check.Script); err != nil {
			return fmt.Errorf("%s/%s: check: %w", ex.ID, check.ID, err)
		}
	}
	if ex.ValidationBaseCase != "" && !caseIDs[ex.ValidationBaseCase] {
		return fmt.Errorf("%s: validation base_case %q is not a declared case",
			ex.ID, ex.ValidationBaseCase)
	}
	return nil
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

func (e *Exercise) WithReference() *Exercise {
	copy := *e
	copy.Source = e.Solution
	copy.Reference = true
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
