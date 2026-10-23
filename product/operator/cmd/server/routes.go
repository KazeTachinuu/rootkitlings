package main

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"time"

	"github.com/coder/websocket"
)

func (o *Operator) handleAgent(w http.ResponseWriter, r *http.Request) {
	if r.Header.Get("Upgrade") == "websocket" {
		o.handleAgentWS(w, r)
		return
	}
	o.serveDecoy(w, r)
}

func (o *Operator) handleAgentWS(w http.ResponseWriter, r *http.Request) {
	conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	_, msg, err := conn.Read(ctx)
	cancel()
	if err != nil || len(msg) < 32+12+16 {
		conn.CloseNow()
		return
	}

	agentIDHex, key, err := o.Keys.DeriveAgent(msg[:32])
	if err != nil {
		conn.CloseNow()
		return
	}
	if _, err := openAEAD(key, msg[32:]); err != nil {
		conn.CloseNow()
		return
	}

	session := o.Agents.register(agentIDHex, conn, key)
	session.remoteAddr = r.RemoteAddr
	o.PublishEvent(Event{Kind: "agent.connected", Agent: agentIDHex, Message: agentIDHex + " connected"})
	go o.refreshAgentInfo(agentIDHex)

	session.readPump()

	o.Agents.remove(agentIDHex)
	o.PublishEvent(Event{Kind: "agent.disconnected", Agent: agentIDHex, Message: agentIDHex + " disconnected"})
}

func (o *Operator) serveDecoy(w http.ResponseWriter, r *http.Request) {
	if r != nil && (r.URL.Path == "/favicon.ico" || r.URL.Path == "/favicon.png") {
		w.Header().Set("Content-Type", "image/png")
		w.Header().Set("Cache-Control", "public, max-age=86400")
		w.Write(faviconPNG)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	io.WriteString(w, decoyHTML)
}

func (o *Operator) refreshAgentInfo(agentID string) {
	reply, err := o.SendCommand(agentID, "INFO", "")
	if err != nil {
		return
	}
	var info agentInfo
	if json.Unmarshal(reply, &info) != nil {
		return
	}
	o.stateMu.Lock()
	o.agentState[agentID] = &AgentState{Info: info, Online: true, LastRefresh: time.Now()}
	o.stateMu.Unlock()
}
