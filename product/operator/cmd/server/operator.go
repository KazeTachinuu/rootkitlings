package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"sync"
	"time"
)

type Operator struct {
	Agents *agentRegistry
	Keys   *OperatorKeys
	Store  *Store
	Events *EventBus

	listenAddr string
	webAddr    string
	tuiEnabled bool
	tuiProgram interface{ Quit() }
	dataDir    string
	log        *slog.Logger

	stateMu    sync.RWMutex
	agentState map[string]*AgentState
}

type AgentState struct {
	Info        agentInfo
	Online      bool
	LastRefresh time.Time
}

type OperatorOption func(*Operator)

func WithWebAddr(addr string) OperatorOption { return func(o *Operator) { o.webAddr = addr } }
func WithTUI(enabled bool) OperatorOption    { return func(o *Operator) { o.tuiEnabled = enabled } }
func WithStore(s *Store) OperatorOption      { return func(o *Operator) { o.Store = s } }
func WithDataDir(dir string) OperatorOption  { return func(o *Operator) { o.dataDir = dir } }

func NewOperator(keys *OperatorKeys, listenAddr string, opts ...OperatorOption) *Operator {
	o := &Operator{
		Agents:     newAgentRegistry(),
		Keys:       keys,
		listenAddr: listenAddr,
		webAddr:    "127.0.0.1:8080",
		agentState: make(map[string]*AgentState),
		Events:     NewEventBus(),
		log:        slog.Default(),
	}
	for _, opt := range opts {
		opt(o)
	}
	return o
}

func (o *Operator) Run(ctx context.Context) error {
	mux := http.NewServeMux()
	mux.HandleFunc("/", o.handleAgent)

	srv := &http.Server{
		Handler:      mux,
		ReadTimeout:  30 * time.Second,
		WriteTimeout: 30 * time.Second,
		IdleTimeout:  120 * time.Second,
	}

	ln, err := net.Listen("tcp", o.listenAddr)
	if err != nil {
		return err
	}

	go o.startWeb(ctx)
	go o.refreshLoop(ctx)
	if o.tuiEnabled {
		go o.startTUI()
	}

	if !o.tuiEnabled {
		fmt.Printf("[*] Agent on %s\n", o.listenAddr)
		fmt.Printf("[*] Dashboard on http://%s\n", o.webAddr)
	}

	go func() {
		<-ctx.Done()
		srv.Shutdown(context.Background())
	}()

	if err := srv.Serve(ln); err != http.ErrServerClosed {
		return err
	}
	return nil
}

func (o *Operator) refreshLoop(ctx context.Context) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			o.refreshAgentState()
		}
	}
}

func (o *Operator) refreshAgentState() {
	sessions, _ := o.Agents.snapshot()
	for _, s := range sessions {
		if !s.isOnline() {
			continue
		}
		go o.refreshAgentInfo(s.id)
	}
}

type CmdOption func(*cmdOptions)

type cmdOptions struct {
	data    json.RawMessage
	timeout time.Duration
}

func WithData(d json.RawMessage) CmdOption {
	return func(o *cmdOptions) { o.data = d }
}

func WithTimeout(d time.Duration) CmdOption {
	return func(o *cmdOptions) { o.timeout = d }
}

func (o *Operator) SendCommand(id, opCode, arg string, opts ...CmdOption) (json.RawMessage, error) {
	co := cmdOptions{timeout: 2 * time.Minute}
	for _, opt := range opts {
		opt(&co)
	}

	s, err := o.Agents.get(id)
	if err != nil {
		return nil, err
	}
	s.cmdMu.Lock()
	defer s.cmdMu.Unlock()

	cmd, _ := json.Marshal(agentCmd{Op: opCode, Arg: arg, Data: co.data})
	ct, err := sealAEAD(s.key, cmd)
	if err != nil {
		return nil, err
	}
	if err := s.writeMessage(ct); err != nil {
		return nil, err
	}

	select {
	case resp := <-s.respCh:
		return resp, nil
	case <-s.done:
		return nil, fmt.Errorf("agent %s disconnected", id)
	case <-time.After(co.timeout):
		return nil, fmt.Errorf("timeout waiting for agent %s", id)
	}
}

func (o *Operator) SendDownload(id, path string) ([]byte, error) {
	s, err := o.Agents.get(id)
	if err != nil {
		return nil, err
	}
	s.cmdMu.Lock()
	defer s.cmdMu.Unlock()

	ch := make(chan []byte, 256)
	s.dlMu.Lock()
	s.dlCh = ch
	s.dlMu.Unlock()
	defer func() {
		s.dlMu.Lock()
		if s.dlCh == ch {
			s.dlCh = nil
		}
		s.dlMu.Unlock()
	}()

	cmd, _ := json.Marshal(agentCmd{Op: "DOWNLOAD", Arg: path})
	ct, err := sealAEAD(s.key, cmd)
	if err != nil {
		return nil, err
	}
	if err := s.writeMessage(ct); err != nil {
		return nil, err
	}

	var buf []byte
	timeout := time.After(60 * time.Second)
	for {
		select {
		case chunk, ok := <-ch:
			if !ok {
				return buf, nil
			}
			buf = append(buf, chunk...)
			if len(buf) > 16<<20 {
				return nil, fmt.Errorf("download too large")
			}
		case <-s.done:
			return nil, fmt.Errorf("agent disconnected")
		case <-timeout:
			return nil, fmt.Errorf("download timeout")
		}
	}
}

func (o *Operator) PublishEvent(e Event) {
	o.Events.Publish(e)
	if o.Store != nil {
		_ = o.Store.InsertAudit(e.Kind, e.Agent, e.Message, e.Data)
	}
}

func (o *Operator) Shutdown() {
	if o.tuiProgram != nil {
		o.tuiProgram.Quit()
	}
	if o.Store != nil {
		o.Store.Close()
	}
	o.Events.Shutdown()
}
