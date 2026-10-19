package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
	"github.com/reeflective/console"
)

var (
	successStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("10"))
	errorStyle   = lipgloss.NewStyle().Foreground(lipgloss.Color("9"))
	dimStyle     = lipgloss.NewStyle().Foreground(lipgloss.Color("8"))
)

// startTUI launches one operator console for the server lifetime.
func startTUI() {
	app := console.New("rklings")

	// TODO: register "ping" command (send PING, print PONG + round-trip time)
	// TODO: register "status" command (send STATUS, print agent state)
	// TODO: register "shell" command (send SHELL <args>, print output until "." line)
	// TODO: register "download" command (send DOWNLOAD <path>, save file locally)
	// TODO: register "upload" command (read local file, send UPLOAD <path> + size + bytes)
	// TODO: register "hide" command with subcommands (file, pid, port, content)
	// TODO: register "persist" command (send PERSIST)
	// TODO: register "escalate" command (send ESCALATE [pid])

	_ = app

	fmt.Println(dimStyle.Render("Type 'help' for available commands"))

	// TODO: app.Start()
}

// formatDuration renders a duration for display.
func formatDuration(d time.Duration) string {
	if d < time.Millisecond {
		return fmt.Sprintf("%dus", d.Microseconds())
	}
	return fmt.Sprintf("%dms", d.Milliseconds())
}

// Ensure imports compile.
var _ = strings.SplitN
var _ = successStyle
var _ = errorStyle
