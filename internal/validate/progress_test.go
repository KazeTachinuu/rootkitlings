package validate

import (
	"testing"

	"github.com/KazeTachinuu/rootkitlings/internal/catalog"
)

func TestDeploymentProvisioningNeverMarksLearnerProgress(t *testing.T) {
	if shouldMarkProgress(&catalog.Exercise{Kind: "deployment"}) {
		t.Fatal("deployment provisioning was treated as learner completion")
	}
	if shouldMarkProgress(&catalog.Exercise{Reference: true}) {
		t.Fatal("reference validation was treated as learner completion")
	}
}
