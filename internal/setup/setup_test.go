package setup

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/x509"
	"encoding/pem"
	"os"
	"path/filepath"
	"testing"
)

func TestParseGoVersion(t *testing.T) {
	if got := parseGoVersion("go version go1.27.2 linux/amd64"); got != "1.27.2" {
		t.Fatalf("parseGoVersion = %q", got)
	}
}

func TestCompareVersions(t *testing.T) {
	for _, test := range []struct {
		a, b string
		want int
	}{{"1.27", "1.27.0", 0}, {"1.27.1", "1.27", 1}, {"1.26.9", "1.27", -1}} {
		if got := compareVersions(test.a, test.b); got != test.want {
			t.Errorf("compareVersions(%q, %q) = %d, want %d", test.a, test.b, got, test.want)
		}
	}
}

func TestCourseCertificatesUseP256(t *testing.T) {
	dir := t.TempDir()
	if err := ensureCA(dir); err != nil {
		t.Fatal(err)
	}
	if err := ensureAgentCert(dir); err != nil {
		t.Fatal(err)
	}
	if err := ensureServerCert(dir, "localhost"); err != nil {
		t.Fatal(err)
	}

	for _, name := range []string{"ca.crt", "agent.crt", "server.crt"} {
		data, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil {
			t.Fatal(err)
		}
		block, _ := pem.Decode(data)
		if block == nil {
			t.Fatalf("%s is not PEM", name)
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			t.Fatal(err)
		}
		key, ok := cert.PublicKey.(*ecdsa.PublicKey)
		if !ok || key.Curve != elliptic.P256() {
			t.Fatalf("%s does not use ECDSA P-256", name)
		}
	}
}
