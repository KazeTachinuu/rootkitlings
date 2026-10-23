package setup

import "testing"

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
