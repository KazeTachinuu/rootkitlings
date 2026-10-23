package main

import (
	"sync"
	"testing"
	"time"
)

func TestEventBusPubSub(t *testing.T) {
	bus := NewEventBus()
	defer bus.Shutdown()

	var (
		mu       sync.Mutex
		received []Event
	)

	ch, unsub := bus.Subscribe(16)
	defer unsub()
	go func() {
		for e := range ch {
			mu.Lock()
			received = append(received, e)
			mu.Unlock()
		}
	}()

	evt := Event{
		Kind:    "agent.connected",
		Agent:   "test-agent",
		Message: "hello",
		Data:    map[string]string{"ip": "10.0.0.1"},
	}
	bus.Publish(evt)

	time.Sleep(50 * time.Millisecond)

	mu.Lock()
	defer mu.Unlock()
	if len(received) != 1 {
		t.Fatalf("expected 1 event, got %d", len(received))
	}
	got := received[0]
	if got.Kind != "agent.connected" {
		t.Errorf("kind = %q, want %q", got.Kind, "agent.connected")
	}
	if got.Agent != "test-agent" {
		t.Errorf("agent = %q, want %q", got.Agent, "test-agent")
	}
	if got.Time.IsZero() {
		t.Error("time should be set")
	}
}

func TestEventBusMultipleSubscribers(t *testing.T) {
	bus := NewEventBus()
	defer bus.Shutdown()

	ch1, unsub1 := bus.Subscribe(16)
	defer unsub1()
	ch2, unsub2 := bus.Subscribe(16)
	defer unsub2()

	bus.Publish(Event{Kind: "test", Message: "hello"})

	select {
	case <-ch1:
	case <-time.After(time.Second):
		t.Fatal("subscriber 1 did not receive")
	}
	select {
	case <-ch2:
	case <-time.After(time.Second):
		t.Fatal("subscriber 2 did not receive")
	}
}
