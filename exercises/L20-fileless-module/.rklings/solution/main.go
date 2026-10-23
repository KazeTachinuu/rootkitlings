// SPDX-License-Identifier: MIT
//
// Fileless kernel module loader.
// Embeds a .ko at compile time and loads it via memfd + finit_module.
// No file touches disk.
package main

import (
	_ "embed"
	"fmt"
	"os"

	"golang.org/x/sys/unix"
)

//go:embed rklings.ko
var koBytes []byte

func main() {
	if err := loadModule(koBytes); err != nil {
		fmt.Fprintf(os.Stderr, "load failed: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("module loaded (fileless)")
}

func loadModule(ko []byte) error {
	fd, err := unix.MemfdCreate("", unix.MFD_CLOEXEC)
	if err != nil {
		return fmt.Errorf("memfd_create: %w", err)
	}
	defer unix.Close(fd)

	if _, err := unix.Write(fd, ko); err != nil {
		return fmt.Errorf("write: %w", err)
	}

	if err := unix.FinitModule(fd, "", 0); err != nil {
		return fmt.Errorf("finit_module: %w", err)
	}

	return nil
}
