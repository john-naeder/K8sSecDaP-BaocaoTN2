// Package response — Kubernetes client construction.
//
// In production the incident-service runs as a pod with a ServiceAccount
// token mounted at /var/run/secrets/kubernetes.io/serviceaccount/.
// Local development can fall back to ~/.kube/config via KUBECONFIG.
package response

import (
	"fmt"
	"os"

	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/tools/clientcmd"
)

// NewClientset returns a kubernetes.Clientset wired against the cluster
// the pod is running in. If KUBECONFIG is set (local dev), it is honoured;
// otherwise InClusterConfig() is used.
func NewClientset() (*kubernetes.Clientset, error) {
	cfg, err := loadRestConfig()
	if err != nil {
		return nil, fmt.Errorf("kube rest config: %w", err)
	}
	cs, err := kubernetes.NewForConfig(cfg)
	if err != nil {
		return nil, fmt.Errorf("kube clientset: %w", err)
	}
	return cs, nil
}

func loadRestConfig() (*rest.Config, error) {
	if kc := os.Getenv("KUBECONFIG"); kc != "" {
		return clientcmd.BuildConfigFromFlags("", kc)
	}
	return rest.InClusterConfig()
}
