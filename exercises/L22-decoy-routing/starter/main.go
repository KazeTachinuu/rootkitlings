package main

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"log"
	"net/http"
	"os"
)

func main() {
	// Load the rklings CA certificate for verifying agent certs
	caPEM, err := os.ReadFile("ca.crt")
	if err != nil {
		log.Fatalf("read CA cert: %v", err)
	}
	caPool := x509.NewCertPool()
	if !caPool.AppendCertsFromPEM(caPEM) {
		log.Fatal("failed to parse CA cert")
	}

	// Load server TLS certificate and key
	cert, err := tls.LoadX509KeyPair("server.crt", "server.key")
	if err != nil {
		log.Fatalf("load server cert: %v", err)
	}

	// TODO: tls.Config with RequestClientCert (not RequireAndVerify)
	tlsCfg := &tls.Config{
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS13,
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// TODO: inspect r.TLS.PeerCertificates, route to decoy or C2
		fmt.Fprintln(w, "not implemented")
	})

	srv := &http.Server{
		Addr:      ":443",
		TLSConfig: tlsCfg,
		Handler:   mux,
	}

	log.Println("listening on :443")
	log.Fatal(srv.ListenAndServeTLS("", ""))
}

// serveDecoy writes a plausible static website page.
func serveDecoy(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html")
	fmt.Fprint(w, `<!DOCTYPE html>
<html><head><title>Welcome</title></head>
<body><h1>It works!</h1><p>Default web page.</p></body>
</html>`)
}

// TODO: serveC2 handles authenticated agent requests
