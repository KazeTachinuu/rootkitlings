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
	// TODO 1: create a memfd with MFD_CLOEXEC and defer its close

	// TODO 2: write .ko bytes to the memfd

	// TODO 3: load the module with finit_module

	return nil
}
