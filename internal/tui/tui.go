package tui

import (
	"fmt"
	"os"
)

var useColor = os.Getenv("NO_COLOR") == ""

const (
	reset  = "\033[0m"
	green  = "\033[32m"
	red    = "\033[31m"
	yellow = "\033[33m"
	cyan   = "\033[36m"
	dim    = "\033[2m"
)

func c(color, s string) string {
	if !useColor {
		return s
	}
	return color + s + reset
}

func Pass(msg string, args ...any)    { fmt.Printf("  "+c(green, "[+]")+" "+msg+"\n", args...) }
func Fail(msg string, args ...any)    { fmt.Printf("  "+c(red, "[-]")+" "+msg+"\n", args...) }
func Warn(msg string, args ...any)    { fmt.Printf("  "+c(yellow, "[!]")+" "+msg+"\n", args...) }
func Info(msg string, args ...any)    { fmt.Printf("  "+c(cyan, "[*]")+" "+msg+"\n", args...) }
func Pending(msg string, args ...any) { fmt.Printf("  "+c(dim, "[ ]")+" "+msg+"\n", args...) }
func Detail(msg string, args ...any)  { fmt.Printf("      "+c(dim, msg)+"\n", args...) }

func Fatal(msg string, args ...any) {
	Fail(msg, args...)
	os.Exit(1)
}

func Banner(lines []string) {
	fmt.Println()
	for _, line := range lines {
		fmt.Println(c(green, line))
	}
	fmt.Println()
}
