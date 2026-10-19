package operator

import (
	"context"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"time"
)

const (
	LabListenAddr  = ":8443"
	LabAgentAddr   = "10.0.2.2:8443"
	DashboardAddr  = "127.0.0.1:8080"
	DashboardURL   = "http://127.0.0.1:8080"
	startupTimeout = 10 * time.Second
)

type Process struct {
	cmd           *exec.Cmd
	once          sync.Once
	done          chan struct{}
	waitErr       error
	AgentAddr     string
	DashboardAddr string
	RelayURL      string
}

func Serve(ctx context.Context, binary, repoRoot, certDir, listenAddr, dashboardAddr string) (*Process, error) {
	return start(ctx, binary, repoRoot, certDir, listenAddr, dashboardAddr, true)
}

func StartValidation(ctx context.Context, binary, repoRoot, certDir string) (*Process, error) {
	listenAddr, err := availableAddr("0.0.0.0:0")
	if err != nil {
		return nil, err
	}
	dashboardAddr, err := availableAddr("127.0.0.1:0")
	if err != nil {
		return nil, err
	}
	return start(ctx, binary, repoRoot, certDir, listenAddr, dashboardAddr, false)
}

func start(ctx context.Context, binary, repoRoot, certDir, listenAddr, dashboardAddr string, tuiEnabled bool) (*Process, error) {
	for _, addr := range []string{listenAddr, dashboardAddr} {
		if err := requireAvailable(addr); err != nil {
			return nil, err
		}
	}
	cmd := exec.CommandContext(ctx, binary,
		filepath.Join(certDir, "server.crt"),
		filepath.Join(certDir, "server.key"),
		filepath.Join(certDir, "ca.crt"),
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if tuiEnabled {
		cmd.Stdin = os.Stdin
	}
	cmd.Env = append(os.Environ(),
		"RKLINGS_LISTEN_ADDR="+listenAddr,
		"RKLINGS_WEB_DIR="+filepath.Join(repoRoot, "lib", "webapp", "dist"),
		"RKLINGS_WEB_ADDR="+dashboardAddr,
	)
	if !tuiEnabled {
		cmd.Env = append(cmd.Env, "RKLINGS_NO_TUI=1")
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start operator: %w", err)
	}
	_, port, _ := net.SplitHostPort(listenAddr)
	_, dashboardPort, _ := net.SplitHostPort(dashboardAddr)
	p := &Process{
		cmd: cmd, AgentAddr: net.JoinHostPort("10.0.2.2", port), DashboardAddr: dashboardAddr,
		RelayURL: "http://" + net.JoinHostPort("10.0.2.2", dashboardPort), done: make(chan struct{}),
	}
	go func() {
		p.waitErr = cmd.Wait()
		close(p.done)
	}()
	if err := waitForTCP(ctx, dashboardAddr, startupTimeout); err != nil {
		_ = p.Close()
		return nil, err
	}
	return p, nil
}

func requireAvailable(addr string) error {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return fmt.Errorf("address %s is already in use; stop the running lab or choose another port: %w", addr, err)
	}
	return ln.Close()
}

func availableAddr(addr string) (string, error) {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return "", fmt.Errorf("select free address: %w", err)
	}
	selected := ln.Addr().String()
	if err := ln.Close(); err != nil {
		return "", err
	}
	return selected, nil
}

func (p *Process) Close() error {
	p.once.Do(func() {
		if p.cmd.Process != nil {
			_ = p.cmd.Process.Kill()
		}
	})
	<-p.done
	return p.waitErr
}

func (p *Process) Wait() error {
	<-p.done
	return p.waitErr
}

func waitForTCP(ctx context.Context, addr string, timeout time.Duration) error {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	for {
		conn, err := net.DialTimeout("tcp", addr, 250*time.Millisecond)
		if err == nil {
			_ = conn.Close()
			return nil
		}
		select {
		case <-ctx.Done():
			return fmt.Errorf("operator did not listen on %s: %w", addr, ctx.Err())
		case <-ticker.C:
		}
	}
}
