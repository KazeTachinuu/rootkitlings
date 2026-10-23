package validate

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

var ansiEscape = regexp.MustCompile(`\x1b\[[0-9;]*m`)

func diagnosticLines(output string) []string {
	const limit = 8
	lines := make([]string, 0, limit)
	seen := make(map[string]bool)
	for _, line := range strings.Split(output, "\n") {
		line = strings.TrimSpace(ansiEscape.ReplaceAllString(line, ""))
		lower := strings.ToLower(line)
		if line == "" || strings.Contains(line, "[+]") ||
			(!strings.Contains(line, "[-]") &&
				!strings.Contains(lower, "error") &&
				!strings.Contains(lower, "failed")) {
			continue
		}
		if !seen[line] {
			lines = append(lines, line)
			seen[line] = true
		}
		if len(lines) == limit {
			break
		}
	}
	if len(lines) == 0 {
		return []string{"check failed without diagnostic output"}
	}
	return lines
}

func writeFailureLog(dataDir, code, caseID, output string) (string, error) {
	directory := filepath.Join(dataDir, "logs")
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return "", err
	}
	path := filepath.Join(directory, fmt.Sprintf("%s-%s.log",
		strings.ToUpper(code), caseID))
	if err := os.WriteFile(path, []byte(output+"\n"), 0o600); err != nil {
		return "", err
	}
	return path, nil
}
