package main

import (
	"context"
	_ "embed"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
)

//go:embed decoy.html
var decoyHTML string

//go:embed favicon.png
var faviconPNG []byte

func defaultListenAddr() string {
	if os.Getuid() == 0 {
		return ":443"
	}
	return ":8443"
}

func main() {
	pkiDir := os.Getenv("RKLINGS_PKI_DIR")
	if pkiDir == "" {
		log.Fatal("RKLINGS_PKI_DIR is required")
	}

	keys, err := LoadOperatorKeys(pkiDir)
	if err != nil {
		log.Fatalf("operator keys: %v", err)
	}

	if agentPub, err := os.ReadFile(filepath.Join(pkiDir, "agent.pub")); err == nil {
		if id, _, err := keys.DeriveAgent(agentPub); err == nil {
			log.Printf("static agent id: %s", id)
		}
	}

	addr := envOr("RKLINGS_LISTEN_ADDR", defaultListenAddr())
	webAddr := envOr("RKLINGS_WEB_ADDR", "127.0.0.1:8080")
	dataDir := envOr("RKLINGS_DATA_DIR", filepath.Join(pkiDir, "..", "data"))
	tuiEnabled := os.Getenv("RKLINGS_NO_TUI") != "1"

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	store, err := OpenStore(dataDir)
	if err != nil {
		log.Fatalf("store: %v", err)
	}

	var opts []OperatorOption
	opts = append(opts, WithWebAddr(webAddr), WithTUI(tuiEnabled), WithDataDir(dataDir), WithStore(store))

	operator := NewOperator(keys, addr, opts...)
	defer operator.Shutdown()

	if err := operator.Run(ctx); err != nil && ctx.Err() == nil {
		log.Fatalf("operator: %v", err)
	}
}
