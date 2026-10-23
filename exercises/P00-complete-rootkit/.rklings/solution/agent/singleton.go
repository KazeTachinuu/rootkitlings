package main

import (
	"fmt"
	"os"
	"strconv"

	"golang.org/x/sys/unix"
)

var singletonLock *os.File

func acquireSingleton() error {
	if inherited := os.Getenv("RKLINGS_LOCK_FD"); inherited != "" {
		fd, err := strconv.Atoi(inherited)
		if err != nil {
			return fmt.Errorf("invalid inherited lock: %w", err)
		}
		singletonLock = os.NewFile(uintptr(fd), "sd-kerneld-lock")
		_ = os.Unsetenv("RKLINGS_LOCK_FD")
		return nil
	}
	lock, err := os.OpenFile("/run/systemd-kerneld.pid", os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return err
	}
	if err := unix.Flock(int(lock.Fd()), unix.LOCK_EX|unix.LOCK_NB); err != nil {
		_ = lock.Close()
		return fmt.Errorf("lock held: %w", err)
	}
	if _, err := unix.FcntlInt(lock.Fd(), unix.F_SETFD, 0); err != nil {
		_ = lock.Close()
		return err
	}
	singletonLock = lock
	if err := os.Setenv("RKLINGS_LOCK_FD", strconv.Itoa(int(lock.Fd()))); err != nil {
		_ = lock.Close()
		return err
	}
	return nil
}
