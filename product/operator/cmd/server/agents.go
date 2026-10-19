package main

import (
	"bufio"
	"fmt"
	"net"
	"sort"
	"sync"
)

type agentSession struct {
	id   string
	peer string
	conn net.Conn
	r    *bufio.Reader
	io   sync.Mutex
}

type agentRegistry struct {
	sync.RWMutex
	byID        map[string]*agentSession
	active      string
	connections uint64
}

var agents = agentRegistry{byID: make(map[string]*agentSession)}

func (r *agentRegistry) add(id string, conn net.Conn, reader *bufio.Reader) {
	r.Lock()
	defer r.Unlock()
	if previous := r.byID[id]; previous != nil {
		_ = previous.conn.Close()
	}
	r.byID[id] = &agentSession{
		id: id, peer: conn.RemoteAddr().String(), conn: conn,
		r: reader,
	}
	if r.active == "" {
		r.active = id
	}
	r.connections++
}

func (r *agentRegistry) current() (*agentSession, error) {
	r.RLock()
	defer r.RUnlock()
	session := r.byID[r.active]
	if session == nil {
		return nil, fmt.Errorf("no agent selected")
	}
	return session, nil
}

func (r *agentRegistry) get(id string) (*agentSession, error) {
	r.RLock()
	defer r.RUnlock()
	session := r.byID[id]
	if session == nil {
		return nil, fmt.Errorf("agent %q is not connected", id)
	}
	return session, nil
}

func (r *agentRegistry) selectAgent(id string) error {
	r.Lock()
	defer r.Unlock()
	if r.byID[id] == nil {
		return fmt.Errorf("unknown agent %q", id)
	}
	r.active = id
	return nil
}

func (r *agentRegistry) remove(conn net.Conn) string {
	r.Lock()
	defer r.Unlock()
	for id, session := range r.byID {
		if session.conn != conn {
			continue
		}
		delete(r.byID, id)
		if r.active == id {
			r.active = ""
			ids := r.idsLocked()
			if len(ids) > 0 {
				r.active = ids[0]
			}
		}
		return id
	}
	return ""
}

func (r *agentRegistry) snapshot() (sessions []*agentSession, active string, connections uint64) {
	r.RLock()
	defer r.RUnlock()
	ids := r.idsLocked()
	for _, id := range ids {
		sessions = append(sessions, r.byID[id])
	}
	return sessions, r.active, r.connections
}

func (r *agentRegistry) idsLocked() []string {
	ids := make([]string, 0, len(r.byID))
	for id := range r.byID {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	return ids
}
