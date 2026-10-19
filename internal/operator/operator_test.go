package operator

import (
	"context"
	"net"
	"testing"
	"time"
)

func TestRequireAvailableRejectsBoundAddress(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	if err := requireAvailable(listener.Addr().String()); err == nil {
		t.Fatal("bound address was reported as available")
	}
}

func TestWaitForTCP(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	if err := waitForTCP(context.Background(), listener.Addr().String(), time.Second); err != nil {
		t.Fatal(err)
	}
}
