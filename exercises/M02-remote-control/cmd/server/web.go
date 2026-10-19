package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
)

// startWeb serves the operator webapp on 127.0.0.1:8080.
func startWeb() {
	http.HandleFunc("/", handleIndex)
	http.HandleFunc("/api/status", handleStatus)
	http.HandleFunc("/api/command", handleCommand)

	log.Println("[*] Webapp listening on 127.0.0.1:8080")
	if err := http.ListenAndServe("127.0.0.1:8080", nil); err != nil {
		log.Printf("webapp: %v", err)
	}
}

// handleIndex serves a minimal HTML page for the operator.
func handleIndex(w http.ResponseWriter, r *http.Request) {
	// TODO: serve a simple HTML page with:
	//       - agent connection status
	//       - command input form
	//       - output display area
	w.Header().Set("Content-Type", "text/html")
	fmt.Fprint(w, "<html><body><h1>rklings</h1><p>Not implemented</p></body></html>")
}

// handleStatus returns agent connection state as JSON.
func handleStatus(w http.ResponseWriter, r *http.Request) {
	agent.Lock()
	connected := agent.conn != nil
	agent.Unlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{
		"connected": connected,
	})
}

// handleCommand accepts a command via POST and relays it to the agent.
func handleCommand(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}

	// TODO: read command from request body
	// TODO: call sendToAgent with the command
	// TODO: return the response as JSON

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{
		"error": "not implemented",
	})
}
