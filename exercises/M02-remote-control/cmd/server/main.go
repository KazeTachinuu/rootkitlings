package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
)

var agent struct {
	sync.Mutex
	conn net.Conn
	r    *bufio.Reader
	id   string
	peer string
}

var agentIO sync.Mutex

func main() {
	if len(os.Args) < 4 {
		fmt.Fprintf(os.Stderr, "usage: %s <cert> <key> <ca>\n", os.Args[0])
		os.Exit(1)
	}

	addr := os.Getenv("RKLINGS_LISTEN_ADDR")
	if addr == "" {
		addr = ":8443"
	}
	ln := listen(addr, os.Args[1], os.Args[2], os.Args[3])
	defer ln.Close()

	go startWeb()
	if os.Getenv("RKLINGS_NO_TUI") != "1" {
		go startTUI()
	}

	fmt.Printf("[*] Listening on %s for agent connections\n", addr)
	webAddr := os.Getenv("RKLINGS_WEB_ADDR")
	if webAddr == "" {
		webAddr = "127.0.0.1:8080"
	}
	fmt.Printf("[*] Webapp on http://%s\n", webAddr)

	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}

		tlsConn := conn.(*tls.Conn)
		if err := tlsConn.Handshake(); err != nil {
			log.Printf("handshake: %v", err)
			conn.Close()
			continue
		}

		state := tlsConn.ConnectionState()
		if len(state.PeerCertificates) == 0 {
			log.Println("no client certificate, rejecting")
			conn.Close()
			continue
		}

		agentID := state.PeerCertificates[0].Subject.CommonName
		fmt.Printf("[+] Agent connected: %s\n", agentID)

		agent.Lock()
		if agent.conn != nil {
			agent.conn.Close()
		}
		agent.conn = conn
		agent.r = bufio.NewReader(conn)
		agent.id = agentID
		agent.peer = conn.RemoteAddr().String()
		agent.Unlock()
		recordEvent("connect", "agent connected: "+agentID)

	}
}

func listen(addr, certFile, keyFile, caFile string) net.Listener {
	// TODO: load the server keypair and client CA, require a verified client
	// certificate, require TLS 1.3, and return a listener on addr.
	panic("implement authenticated operator listener")
}

func sendToAgent(cmd string) (string, error) {
	// TODO: serialize access with agentIO, snapshot the active connection and
	// reader, send one command, then read its response according to the protocol:
	// dot-terminated SHELL, size-then-bytes DOWNLOAD, or one line otherwise.
	return "", fmt.Errorf("implement serialized operator exchange")
}

func sendUploadToAgent(path string, data []byte) (string, error) {
	agentIO.Lock()
	defer agentIO.Unlock()
	agent.Lock()
	conn, reader := agent.conn, agent.r
	agent.Unlock()
	if conn == nil || reader == nil {
		return "", fmt.Errorf("no agent connected")
	}
	if _, err := fmt.Fprintf(conn, "UPLOAD %s\n%d\n", path, len(data)); err != nil {
		setOffline(conn)
		return "", err
	}
	if _, err := conn.Write(data); err != nil {
		setOffline(conn)
		return "", err
	}
	reply, err := reader.ReadString('\n')
	if err != nil {
		setOffline(conn)
		return "", err
	}
	return strings.TrimSpace(reply), nil
}

func setOffline(conn net.Conn) {
	disconnected := ""
	agent.Lock()
	if agent.conn == conn {
		disconnected = agent.id
		agent.conn = nil
		agent.r = nil
	}
	agent.Unlock()
	if disconnected != "" {
		recordEvent("disconnect", "agent disconnected: "+disconnected)
	}
}
