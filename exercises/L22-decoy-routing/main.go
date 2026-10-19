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
	certFile, keyFile, caFile := tlsFiles()

	// Load the rklings CA certificate for verifying agent certs
	caPEM, err := os.ReadFile(caFile)
	if err != nil {
		log.Fatalf("read CA cert: %v", err)
	}
	caPool := x509.NewCertPool()
	if !caPool.AppendCertsFromPEM(caPEM) {
		log.Fatal("failed to parse CA cert")
	}

	// Load server TLS certificate and key
	cert, err := tls.LoadX509KeyPair(certFile, keyFile)
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
		Addr:      listenAddr(),
		TLSConfig: tlsCfg,
		Handler:   mux,
	}

	log.Printf("listening on %s", srv.Addr)
	log.Fatal(srv.ListenAndServeTLS("", ""))
}

func tlsFiles() (string, string, string) {
	if len(os.Args) == 4 {
		return os.Args[1], os.Args[2], os.Args[3]
	}
	return "server.crt", "server.key", "ca.crt"
}

func listenAddr() string {
	if addr := os.Getenv("RKLINGS_LISTEN_ADDR"); addr != "" {
		return addr
	}
	return ":443"
}

// serveDecoy writes a plausible static website page.
func serveDecoy(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprint(w, `<!DOCTYPE html>
<html><head><title>EduSigh</title></head>
<body><h1>Attendance, powered by one heroic administrator.</h1>
<p>Pedro is resending your signature email. Please keep your QR code ready.</p></body>
</html>`)
}

// TODO: serveC2 handles authenticated agent requests
