package main

import (
	"sync"
	"time"
)

type Event struct {
	Kind    string            `json:"kind"`
	Agent   string            `json:"agent,omitempty"`
	Message string            `json:"message"`
	Time    time.Time         `json:"time"`
	Data    map[string]string `json:"data,omitempty"`
}

type EventBus struct {
	mu   sync.RWMutex
	subs map[int]chan Event
	next int
	done chan struct{}
}

func NewEventBus() *EventBus {
	return &EventBus{
		subs: make(map[int]chan Event),
		done: make(chan struct{}),
	}
}

func (b *EventBus) Publish(e Event) {
	if e.Time.IsZero() {
		e.Time = time.Now()
	}
	b.mu.RLock()
	defer b.mu.RUnlock()
	for _, ch := range b.subs {
		select {
		case ch <- e:
		default:
		}
	}
}

func (b *EventBus) Subscribe(bufSize int) (<-chan Event, func()) {
	ch := make(chan Event, bufSize)
	b.mu.Lock()
	id := b.next
	b.next++
	b.subs[id] = ch
	b.mu.Unlock()
	return ch, func() {
		b.mu.Lock()
		delete(b.subs, id)
		b.mu.Unlock()
		close(ch)
	}
}

func (b *EventBus) Shutdown() {
	b.mu.Lock()
	defer b.mu.Unlock()
	for id, ch := range b.subs {
		close(ch)
		delete(b.subs, id)
	}
	select {
	case <-b.done:
	default:
		close(b.done)
	}
}
