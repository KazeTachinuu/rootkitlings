package main

import (
	"testing"
)

func TestOpenStoreMigrateIdempotent(t *testing.T) {
	dir := t.TempDir()
	s, err := OpenStore(dir)
	if err != nil {
		t.Fatalf("first open: %v", err)
	}
	s.Close()

	s, err = OpenStore(dir)
	if err != nil {
		t.Fatalf("second open (idempotency): %v", err)
	}
	s.Close()
}

func TestInsertAuditRecentOrdering(t *testing.T) {
	s, err := OpenStore(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()

	for i := 0; i < 5; i++ {
		err := s.InsertAudit("connect", "a1", "msg"+string(rune('0'+i)), nil)
		if err != nil {
			t.Fatal(err)
		}
	}

	entries, err := s.RecentAudit(3)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 3 {
		t.Fatalf("expected 3 entries, got %d", len(entries))
	}
	if entries[0].Message != "msg4" {
		t.Fatalf("expected most recent msg4, got %s", entries[0].Message)
	}
	if entries[2].Message != "msg2" {
		t.Fatalf("expected oldest msg2, got %s", entries[2].Message)
	}
}

