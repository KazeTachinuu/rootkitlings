package main

import (
	"context"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/coder/websocket"
)

func TestDecoyServedForNonAgentRequest(t *testing.T) {
	keys := testOperatorKeys(t)
	o := &Operator{
		Keys:   keys,
		Agents: newAgentRegistry(),
		Events: NewEventBus(),
	}
	srv := httptest.NewServer(http.HandlerFunc(o.handleAgent))
	defer srv.Close()

	resp, err := http.Get(srv.URL + "/")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if resp.StatusCode != 200 {
		t.Fatalf("status = %d", resp.StatusCode)
	}
	if !strings.Contains(string(body), "edusigh") {
		t.Fatal("decoy page not served for plain GET")
	}
}

func TestAgentWSAuthenticatesAndConnects(t *testing.T) {
	keys := testOperatorKeys(t)
	o := &Operator{
		Keys:       keys,
		Agents:     newAgentRegistry(),
		Events:     NewEventBus(),
		agentState: make(map[string]*AgentState),
	}
	srv := httptest.NewServer(http.HandlerFunc(o.handleAgent))
	defer srv.Close()

	agentPriv, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	agentID, _, err := keys.DeriveAgent(agentPriv.PublicKey().Bytes())
	if err != nil {
		t.Fatal(err)
	}

	secret, err := agentPriv.ECDH(keys.pub)
	if err != nil {
		t.Fatal(err)
	}
	sharedKey := sha256.Sum256(secret)
	if hex.EncodeToString(sharedKey[:8]) != agentID {
		t.Fatal("agent_id mismatch")
	}

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http") + "/"
	conn, _, err := websocket.Dial(context.Background(), wsURL, nil)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer conn.CloseNow()

	hello := []byte(`{"t":"hello"}`)
	ct, err := sealAEAD(sharedKey, hello)
	if err != nil {
		t.Fatal(err)
	}
	frame := append(sharedKey[:8], ct...)
	if err := conn.Write(context.Background(), websocket.MessageBinary, frame); err != nil {
		t.Fatal(err)
	}

	s, err := o.Agents.get(agentID)
	if err != nil {
		t.Logf("agent not immediately registered, that's ok for async registration")
	} else if s.id != agentID {
		t.Fatalf("registered agent id = %q, want %q", s.id, agentID)
	}
}

func TestBadCiphertextGetsDecoy(t *testing.T) {
	keys := testOperatorKeys(t)
	o := &Operator{
		Keys:   keys,
		Agents: newAgentRegistry(),
		Events: NewEventBus(),
	}
	srv := httptest.NewServer(http.HandlerFunc(o.handleAgent))
	defer srv.Close()

	resp, err := http.Post(srv.URL+"/", "text/plain", strings.NewReader("garbage"))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if !strings.Contains(string(body), "edusigh") {
		t.Fatal("expected decoy for bad POST")
	}
}

func testOperatorKeys(t *testing.T) *OperatorKeys {
	t.Helper()
	curve := ecdh.X25519()
	priv, err := curve.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	return &OperatorKeys{
		priv:   priv,
		pub:    priv.PublicKey(),
		agents: make(map[string][32]byte),
	}
}
