package main

import (
	"bufio"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"io"
	"math/big"
	"net"
	"net/http"
	"strings"
	"testing"
	"time"
)

func TestTrustedAgentRouting(t *testing.T) {
	ca, caKey := testCertificate(t, nil, nil, true, nil)
	agent, _ := testCertificate(t, ca, caKey, false,
		[]x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth})
	outsider, _ := testCertificate(t, nil, nil, true,
		[]x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth})
	roots := x509.NewCertPool()
	roots.AddCert(ca)

	if trustedAgent(tls.ConnectionState{}, roots) {
		t.Fatal("client without a certificate was trusted")
	}
	if trustedAgent(tls.ConnectionState{PeerCertificates: []*x509.Certificate{outsider}}, roots) {
		t.Fatal("certificate outside the course CA was trusted")
	}
	if !trustedAgent(tls.ConnectionState{PeerCertificates: []*x509.Certificate{agent}}, roots) {
		t.Fatal("course-signed client certificate was rejected")
	}
}

func TestDecoyIsRealHTTPResponse(t *testing.T) {
	server, client := net.Pipe()
	t.Cleanup(func() { _ = client.Close() })
	go serveDecoy(server, bufio.NewReader(server))
	_, _ = io.WriteString(client, "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n")
	response, err := http.ReadResponse(bufio.NewReader(client), nil)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	body, _ := io.ReadAll(response.Body)
	if response.StatusCode != http.StatusOK || !strings.Contains(string(body), "heroic administrator") {
		t.Fatalf("unexpected decoy response: %s %q", response.Status, body)
	}
	if got := response.Header.Get("Content-Security-Policy"); got == "" {
		t.Fatal("decoy response has no content security policy")
	}
}

func testCertificate(t *testing.T, parent *x509.Certificate,
	parentKey *ecdsa.PrivateKey, isCA bool,
	usage []x509.ExtKeyUsage) (*x509.Certificate, *ecdsa.PrivateKey) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{
		SerialNumber: big.NewInt(time.Now().UnixNano()),
		Subject:      pkix.Name{CommonName: "test"},
		NotBefore:    time.Now().Add(-time.Minute), NotAfter: time.Now().Add(time.Hour),
		IsCA: isCA, BasicConstraintsValid: true, ExtKeyUsage: usage,
		KeyUsage: x509.KeyUsageDigitalSignature,
	}
	if isCA {
		template.KeyUsage |= x509.KeyUsageCertSign
	}
	if parent == nil {
		parent, parentKey = template, key
	}
	der, err := x509.CreateCertificate(rand.Reader, template, parent, &key.PublicKey, parentKey)
	if err != nil {
		t.Fatal(err)
	}
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	return cert, key
}
