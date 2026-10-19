package labserver

import "testing"

func TestAllowedCommands(t *testing.T) {
	commands := []string{"PING", "SHELL", "DOWNLOAD"}
	tests := []struct {
		command string
		want    bool
	}{
		{"PING", true},
		{"SHELL id", true},
		{"DOWNLOAD /etc/hostname", true},
		{"UPLOAD /tmp/file", false},
		{"SHELLBACK id", false},
		{"", false},
	}
	for _, test := range tests {
		if got := allowed(commands, test.command); got != test.want {
			t.Errorf("allowed(%q) = %v, want %v", test.command, got, test.want)
		}
	}
}
