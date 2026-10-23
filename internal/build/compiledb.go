package build

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/KazeTachinuu/rootkitlings/internal/project"
	"github.com/google/shlex"
)

type compileCommand struct {
	Directory string   `json:"directory"`
	File      string   `json:"file"`
	Arguments []string `json:"arguments"`
}

// CompileDatabase writes editor commands for learner and reference C sources
// using flags captured from the pinned course kernel build.
func CompileDatabase(root string) (int, error) {
	tc, err := loadToolchain("course")
	if err != nil {
		return 0, err
	}
	runtimeName, err := DetectRuntime()
	if err != nil {
		return 0, err
	}
	if err := ensureBuildImage(runtimeName, tc); err != nil {
		return 0, err
	}

	reference := filepath.Join(root, "exercises", "M00-persistence-stealth",
		".rklings", "solution")
	stage, err := project.TempDir("compile-db-")
	if err != nil {
		return 0, fmt.Errorf("create compile database stage: %w", err)
	}
	defer os.RemoveAll(stage)
	if err := os.CopyFS(stage, os.DirFS(reference)); err != nil {
		return 0, fmt.Errorf("stage reference module: %w", err)
	}

	dataDir, err := project.DataDir()
	if err != nil {
		return 0, fmt.Errorf("project data directory: %w", err)
	}
	headers := filepath.Join(dataDir, "toolchains", tc.ID)
	if err := os.MkdirAll(headers, 0700); err != nil {
		return 0, fmt.Errorf("create toolchain directory: %w", err)
	}

	script := fmt.Sprintf(
		"make -C /lib/modules/%s/build M=/src V=1 modules; "+
			"test -f /out/.ready || { mkdir -p /out/usr/src; "+
			"cp -aL /usr/src/. /out/usr/src/ || true; touch /out/.ready; }",
		tc.Kernel,
	)
	args := []string{"run", "--rm"}
	if runtimeName == "docker" {
		args = append(args, "--user", fmt.Sprintf("%d:%d", os.Getuid(), os.Getgid()))
	}
	args = append(args,
		"-v", stage+":/src",
		"-v", headers+":/out",
		tc.Image, "sh", "-c", script,
	)
	out, err := exec.Command(runtimeName, args...).CombinedOutput()
	if err != nil {
		return 0, fmt.Errorf("capture kernel compile command: %w\n%s", err, out)
	}
	base, err := capturedArguments(string(out))
	if err != nil {
		return 0, err
	}

	sources, err := courseCSources(root)
	if err != nil {
		return 0, err
	}
	commands := make([]compileCommand, 0, len(sources))
	containerHeaders := "/usr/src"
	hostHeaders := filepath.Join(headers, "usr", "src")
	for _, source := range sources {
		arguments := make([]string, 0, len(base)+4)
		for _, argument := range base {
			argument = strings.ReplaceAll(argument, containerHeaders, hostHeaders)
			arguments = append(arguments, argument)
		}
		include := sourceInclude(source)
		if include != "" {
			arguments = append(arguments, "-I"+include)
		}
		arguments = append(arguments, "-c", source)
		commands = append(commands, compileCommand{
			Directory: root,
			File:      source,
			Arguments: arguments,
		})
	}

	data, err := json.MarshalIndent(commands, "", "  ")
	if err != nil {
		return 0, fmt.Errorf("encode compile database: %w", err)
	}
	data = append(data, '\n')
	temporary, err := os.CreateTemp(root, ".compile_commands-*.json")
	if err != nil {
		return 0, fmt.Errorf("create compile database: %w", err)
	}
	temporaryName := temporary.Name()
	defer os.Remove(temporaryName)
	if _, err := temporary.Write(data); err != nil {
		_ = temporary.Close()
		return 0, err
	}
	if err := temporary.Close(); err != nil {
		return 0, err
	}
	if err := os.Rename(temporaryName, filepath.Join(root, "compile_commands.json")); err != nil {
		return 0, fmt.Errorf("install compile database: %w", err)
	}
	return len(commands), nil
}

func capturedArguments(output string) ([]string, error) {
	for _, line := range strings.Split(output, "\n") {
		fields, err := shlex.Split(strings.TrimSpace(line))
		if err != nil || len(fields) == 0 {
			continue
		}
		if !strings.Contains(filepath.Base(fields[0]), "gcc") {
			continue
		}
		source := -1
		for i, field := range fields {
			if strings.HasSuffix(field, ".c") && !strings.HasPrefix(field, "/usr/src/") {
				source = i
			}
		}
		if source < 0 {
			continue
		}
		clean := make([]string, 0, len(fields))
		for i := 0; i < len(fields); i++ {
			field := fields[i]
			if field == "-o" && i+1 < len(fields) {
				i++
				continue
			}
			if field == "-c" || i == source ||
				strings.HasPrefix(field, "-Wp,-MMD,") ||
				strings.HasPrefix(field, "-Wp,-MD,") ||
				strings.HasPrefix(field, "-I.") ||
				strings.HasPrefix(field, "-fsanitize=") {
				continue
			}
			clean = append(clean, field)
		}
		return clean, nil
	}
	return nil, fmt.Errorf("kernel build emitted no C compile command")
}

func courseCSources(root string) ([]string, error) {
	var sources []string
	exercises := filepath.Join(root, "exercises")
	err := filepath.WalkDir(exercises, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() || filepath.Ext(path) != ".c" {
			return nil
		}
		relative, err := filepath.Rel(exercises, path)
		if err != nil {
			return err
		}
		parts := strings.Split(filepath.ToSlash(relative), "/")
		learner := len(parts) >= 2 && parts[1] != ".rklings"
		reference := len(parts) >= 4 && parts[1] == ".rklings" && parts[2] == "solution"
		if learner || reference {
			absolute, err := filepath.Abs(path)
			if err != nil {
				return err
			}
			sources = append(sources, absolute)
		}
		return nil
	})
	return sources, err
}

func sourceInclude(source string) string {
	for directory := filepath.Dir(source); ; directory = filepath.Dir(directory) {
		include := filepath.Join(directory, "include")
		if info, err := os.Stat(include); err == nil && info.IsDir() {
			return include
		}
		base := filepath.Base(directory)
		if base == "solution" || base == "exercises" || filepath.Dir(directory) == directory {
			return ""
		}
	}
}
