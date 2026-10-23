package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"sort"
	"sync"

	"github.com/coder/websocket"
)

type agentCmd struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

type agentSession struct {
	id         string
	conn       *websocket.Conn
	key        [32]byte
	remoteAddr string
	writeMu    sync.Mutex
	cmdMu      sync.Mutex
	respCh     chan json.RawMessage
	ptyMu      sync.RWMutex
	ptySub     chan []byte
	dlMu       sync.RWMutex
	dlCh       chan []byte
	done       chan struct{}
}

func (s *agentSession) isOnline() bool {
	select {
	case <-s.done:
		return false
	default:
		return true
	}
}

func (s *agentSession) writeMessage(data []byte) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	return s.conn.Write(context.Background(), websocket.MessageBinary, data)
}

func (s *agentSession) readPump() {
	defer close(s.done)
	defer func() {
		s.dlMu.Lock()
		if s.dlCh != nil {
			close(s.dlCh)
			s.dlCh = nil
		}
		s.dlMu.Unlock()
	}()
	for {
		_, msg, err := s.conn.Read(context.Background())
		if err != nil {
			return
		}
		plain, err := openAEAD(s.key, msg)
		if err != nil {
			continue
		}
		// Binary download chunks: 0x01 = data, 0x02 = done
		if len(plain) > 0 && (plain[0] == 0x01 || plain[0] == 0x02) {
			s.dlMu.RLock()
			ch := s.dlCh
			s.dlMu.RUnlock()
			if ch != nil {
				if plain[0] == 0x01 {
					select {
					case ch <- plain[1:]:
					default:
					}
				} else {
					s.dlMu.Lock()
					close(s.dlCh)
					s.dlCh = nil
					s.dlMu.Unlock()
				}
			}
			continue
		}
		var frame struct {
			PTY  string          `json:"pty,omitempty"`
			Data json.RawMessage `json:"data,omitempty"`
		}
		if json.Unmarshal(plain, &frame) != nil {
			continue
		}
		if frame.PTY != "" {
			s.ptyMu.RLock()
			sub := s.ptySub
			s.ptyMu.RUnlock()
			if sub != nil {
				data, err := base64.StdEncoding.DecodeString(frame.PTY)
				if err == nil {
					select {
					case sub <- data:
					default:
					}
				}
			}
		} else if frame.Data != nil {
			select {
			case s.respCh <- frame.Data:
			default:
			}
		}
	}
}

type agentRegistry struct {
	mu           sync.RWMutex
	byID         map[string]*agentSession
	active       string
	connectCount int
}

func newAgentRegistry() *agentRegistry {
	return &agentRegistry{byID: make(map[string]*agentSession)}
}

func (r *agentRegistry) register(id string, conn *websocket.Conn, key [32]byte) *agentSession {
	r.mu.Lock()
	defer r.mu.Unlock()
	if old, ok := r.byID[id]; ok && old.conn != nil {
		old.conn.CloseNow()
	}
	s := &agentSession{
		id:     id,
		conn:   conn,
		key:    key,
		respCh: make(chan json.RawMessage, 1),
		done:   make(chan struct{}),
	}
	r.byID[id] = s
	r.connectCount++
	if r.active == "" {
		r.active = id
	}
	return s
}

func (r *agentRegistry) current() (*agentSession, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	s := r.byID[r.active]
	if s == nil {
		return nil, fmt.Errorf("no agent selected")
	}
	return s, nil
}

func (r *agentRegistry) get(id string) (*agentSession, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	s := r.byID[id]
	if s == nil {
		return nil, fmt.Errorf("agent %q not connected", id)
	}
	return s, nil
}

func (r *agentRegistry) selectAgent(id string) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.byID[id] == nil {
		return fmt.Errorf("unknown agent %q", id)
	}
	r.active = id
	return nil
}

func (r *agentRegistry) remove(id string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	delete(r.byID, id)
	if r.active == id {
		r.active = ""
		for newID := range r.byID {
			r.active = newID
			break
		}
	}
}

func (r *agentRegistry) snapshot() ([]*agentSession, string) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	ids := make([]string, 0, len(r.byID))
	for id := range r.byID {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	sessions := make([]*agentSession, 0, len(ids))
	for _, id := range ids {
		sessions = append(sessions, r.byID[id])
	}
	return sessions, r.active
}
