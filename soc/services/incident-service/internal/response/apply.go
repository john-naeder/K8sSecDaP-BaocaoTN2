package response

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	networkingv1 "k8s.io/api/networking/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"sigs.k8s.io/yaml"
)

// Applier abstracts how an approved NetworkPolicy is materialised.
//
// Two implementations:
//   - DryRunApplier: writes the YAML to a PVC for analyst review (default).
//   - K8sApiApplier: calls NetworkingV1().NetworkPolicies(ns).Create/Update
//     using the pod's ServiceAccount via in-cluster config.
type Applier interface {
	Apply(ctx context.Context, yaml string, incidentID int64) (string, error)
}

// NetworkPolicyClient is the subset of clientset used by K8sApiApplier.
// Defining it lets unit tests swap in a fake.
type NetworkPolicyClient interface {
	CreateOrUpdate(ctx context.Context, np *networkingv1.NetworkPolicy) error
}

func NewApplier(mode, draftDir string) (Applier, error) {
	switch mode {
	case "", "dryrun":
		if draftDir == "" {
			draftDir = "/var/lib/zt/netpol-drafts"
		}
		if err := os.MkdirAll(draftDir, 0o755); err != nil {
			return nil, fmt.Errorf("mkdir draft dir: %w", err)
		}
		return &DryRunApplier{Dir: draftDir}, nil
	case "apply":
		cs, err := NewClientset()
		if err != nil {
			return nil, err
		}
		return &K8sApiApplier{Client: clientsetNPAdapter{cs: cs}}, nil
	default:
		return nil, fmt.Errorf("unknown APPLY_MODE %q", mode)
	}
}

type DryRunApplier struct {
	Dir string
}

func (d *DryRunApplier) Apply(_ context.Context, yamlBody string, incidentID int64) (string, error) {
	name := fmt.Sprintf("incident-%d-%d.yaml", incidentID, time.Now().Unix())
	path := filepath.Join(d.Dir, name)
	if err := os.WriteFile(path, []byte(yamlBody), 0o644); err != nil {
		return "", err
	}
	return path, nil
}

// K8sApiApplier parses YAML draft into a NetworkPolicy and creates/updates
// it via NetworkPolicyClient.
type K8sApiApplier struct {
	Client NetworkPolicyClient
}

func (k *K8sApiApplier) Apply(ctx context.Context, yamlBody string, incidentID int64) (string, error) {
	if k.Client == nil {
		return "", errors.New("K8sApiApplier: nil Client")
	}
	np := &networkingv1.NetworkPolicy{}
	if err := yaml.Unmarshal([]byte(yamlBody), np); err != nil {
		return "", fmt.Errorf("parse NetworkPolicy YAML: %w", err)
	}
	if np.Name == "" || np.Namespace == "" {
		return "", fmt.Errorf("NetworkPolicy missing name/namespace (got name=%q namespace=%q)", np.Name, np.Namespace)
	}
	if err := k.Client.CreateOrUpdate(ctx, np); err != nil {
		return "", fmt.Errorf("apply %s/%s: %w", np.Namespace, np.Name, err)
	}
	return fmt.Sprintf("networkpolicy.networking.k8s.io/%s -n %s", np.Name, np.Namespace), nil
}

// clientsetNPAdapter wraps a real clientset to satisfy NetworkPolicyClient.
type clientsetNPAdapter struct {
	cs *kubernetes.Clientset
}

func (a clientsetNPAdapter) CreateOrUpdate(ctx context.Context, np *networkingv1.NetworkPolicy) error {
	api := a.cs.NetworkingV1().NetworkPolicies(np.Namespace)
	_, err := api.Create(ctx, np, metav1.CreateOptions{})
	if err == nil {
		return nil
	}
	if !apierrors.IsAlreadyExists(err) {
		return err
	}
	existing, getErr := api.Get(ctx, np.Name, metav1.GetOptions{})
	if getErr != nil {
		return getErr
	}
	np.ResourceVersion = existing.ResourceVersion
	_, err = api.Update(ctx, np, metav1.UpdateOptions{})
	return err
}
