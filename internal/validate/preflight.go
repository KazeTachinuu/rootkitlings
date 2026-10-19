package validate

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
)

type todoLocation struct {
	file   string
	line   int
	column int
	text   string
}

// CheckWorkspace reports unfinished learner work before a command invokes a
// compiler or boots a VM. Reference workspaces do not contain learner TODOs.
func CheckWorkspace(root string, ex *catalog.Exercise) error {
	if ex.Reference {
		return nil
	}
	locations, err := unfinishedTODOs(root, ex)
	if err != nil {
		return fmt.Errorf("scan learner TODOs: %w", err)
	}
	if len(locations) == 0 {
		return nil
	}
	return unfinishedError(locations)
}

func unfinishedTODOs(root string, ex *catalog.Exercise) ([]todoLocation, error) {
	var locations []todoLocation
	for _, relative := range ex.EditableFiles {
		path := filepath.Join(ex.Source, relative)
		displayPath, err := filepath.Rel(root, path)
		if err != nil {
			return nil, err
		}
		file, err := os.Open(path)
		if err != nil {
			return nil, err
		}
		scanner := bufio.NewScanner(file)
		for line := 1; scanner.Scan(); line++ {
			sourceLine := scanner.Text()
			text := strings.TrimSpace(sourceLine)
			if strings.Contains(text, "TODO") {
				locations = append(locations, todoLocation{
					file:   filepath.ToSlash(displayPath),
					line:   line,
					column: strings.Index(sourceLine, "TODO") + 1,
					text:   trimTODO(text),
				})
			}
		}
		scanErr := scanner.Err()
		closeErr := file.Close()
		if scanErr != nil {
			return nil, scanErr
		}
		if closeErr != nil {
			return nil, closeErr
		}
	}
	return locations, nil
}

func trimTODO(text string) string {
	text = strings.TrimSpace(strings.TrimLeft(text, "#/ *"))
	text = strings.TrimSpace(strings.TrimPrefix(text, "TODO:"))
	text = strings.TrimSpace(strings.TrimSuffix(text, "*/"))
	return text
}

func unfinishedError(locations []todoLocation) error {
	label := "TODO remains"
	if len(locations) != 1 {
		label = "TODOs remain"
	}
	var message strings.Builder
	fmt.Fprintf(&message, "%d unfinished %s", len(locations), label)
	for _, location := range locations {
		fmt.Fprintf(&message, "\n      %s:%d:%d: %s", location.file,
			location.line, location.column, location.text)
	}
	if len(locations) == 1 {
		message.WriteString("\n      complete it before continuing")
	} else {
		message.WriteString("\n      complete them before continuing")
	}
	return fmt.Errorf("%s", message.String())
}
