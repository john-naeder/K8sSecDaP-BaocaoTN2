// Package response — pod-level remediation actions (Layer-2).
//
// After NetworkPolicy quarantine (Layer-1) blocks lateral movement, an
// admin can choose between deleting the pod (if backed by a controller
// it will be replaced) or "shutdown" — patch a marker label, then delete
// with a long grace period so logs can be captured before eviction.
package response

import (
	"context"
	"encoding/json"
	"fmt"

	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/client-go/kubernetes"
)

// PodClient is the subset of CoreV1().Pods() we use. Defined as an
// interface so tests can swap in a fake clientset.
type PodClient interface {
	Get(ctx context.Context, namespace, name string) (*corev1.Pod, error)
	List(ctx context.Context, namespace string) ([]corev1.Pod, error)
	Delete(ctx context.Context, namespace, name string, gracePeriodSeconds int64) error
	Patch(ctx context.Context, namespace, name string, patch []byte) error
}

// PodOps wraps a PodClient with high-level remediation operations.
type PodOps struct {
	Client PodClient
}

// DeletePod removes the pod immediately (grace period 0). For a pod owned
// by a Deployment/StatefulSet/DaemonSet the controller will recreate it.
// For a bare Pod the deletion is permanent.
func (p *PodOps) DeletePod(ctx context.Context, namespace, name string) error {
	if _, err := p.Client.Get(ctx, namespace, name); err != nil {
		if apierrors.IsNotFound(err) {
			return fmt.Errorf("pod %s/%s not found", namespace, name)
		}
		return fmt.Errorf("get pod %s/%s: %w", namespace, name, err)
	}
	if err := p.Client.Delete(ctx, namespace, name, 0); err != nil {
		return fmt.Errorf("delete pod %s/%s: %w", namespace, name, err)
	}
	return nil
}

// ShutdownPod tags the pod with zt-soc/shutdown=true and deletes it with
// a 5-minute grace period. The label gives an analyst time to exec into
// the pod and grab logs before the kubelet evicts it.
func (p *PodOps) ShutdownPod(ctx context.Context, namespace, name string) error {
	if _, err := p.Client.Get(ctx, namespace, name); err != nil {
		if apierrors.IsNotFound(err) {
			return fmt.Errorf("pod %s/%s not found", namespace, name)
		}
		return fmt.Errorf("get pod %s/%s: %w", namespace, name, err)
	}
	patch := map[string]any{
		"metadata": map[string]any{
			"labels": map[string]string{
				"zt-soc/shutdown": "true",
			},
		},
	}
	body, err := json.Marshal(patch)
	if err != nil {
		return err
	}
	if err := p.Client.Patch(ctx, namespace, name, body); err != nil {
		return fmt.Errorf("patch shutdown label %s/%s: %w", namespace, name, err)
	}
	if err := p.Client.Delete(ctx, namespace, name, 300); err != nil {
		return fmt.Errorf("graceful delete %s/%s: %w", namespace, name, err)
	}
	return nil
}

// LookupPodByIP returns the name of the Pod in namespace whose status.PodIP
// matches ip. found=false (nil error) means no such pod — callers treat that as
// "not in this namespace" rather than a failure.
func (p *PodOps) LookupPodByIP(ctx context.Context, namespace, ip string) (string, bool, error) {
	pods, err := p.Client.List(ctx, namespace)
	if err != nil {
		return "", false, fmt.Errorf("list pods in %s: %w", namespace, err)
	}
	for i := range pods {
		if pods[i].Status.PodIP == ip {
			return pods[i].Name, true, nil
		}
	}
	return "", false, nil
}

// QuarantinePodByIP finds the Pod in namespace whose status.PodIP == ip and
// patches labelKey=true onto it, so the per-incident egress-deny NetworkPolicy
// (see GenerateQuarantine) can select it. Returns the matched pod name. A
// missing pod is reported as an error the caller may log without aborting —
// the NetworkPolicy is still useful once the pod is (re)labelled.
func (p *PodOps) QuarantinePodByIP(ctx context.Context, namespace, ip, labelKey string) (string, error) {
	name, found, err := p.LookupPodByIP(ctx, namespace, ip)
	if err != nil {
		return "", err
	}
	if !found {
		return "", fmt.Errorf("no pod with IP %s in namespace %s", ip, namespace)
	}
	patch := map[string]any{
		"metadata": map[string]any{
			"labels": map[string]string{labelKey: "true"},
		},
	}
	body, err := json.Marshal(patch)
	if err != nil {
		return "", err
	}
	if err := p.Client.Patch(ctx, namespace, name, body); err != nil {
		return "", fmt.Errorf("label pod %s/%s: %w", namespace, name, err)
	}
	return name, nil
}

// NewPodOpsFromClientset wraps an existing Clientset (or test fake — both
// kubernetes.Interface).
func NewPodOpsFromClientset(cs kubernetes.Interface) *PodOps {
	return &PodOps{Client: clientsetPodAdapter{cs: cs}}
}

// NewPodOps wires PodOps against the in-cluster Clientset. Returns nil
// if Kubernetes config is unavailable (e.g. local dev with no cluster);
// the caller should treat that as "podops disabled" rather than fatal.
func NewPodOps() (*PodOps, error) {
	cs, err := NewClientset()
	if err != nil {
		return nil, err
	}
	return NewPodOpsFromClientset(cs), nil
}

type clientsetPodAdapter struct {
	cs kubernetes.Interface
}

func (a clientsetPodAdapter) Get(ctx context.Context, ns, name string) (*corev1.Pod, error) {
	return a.cs.CoreV1().Pods(ns).Get(ctx, name, metav1.GetOptions{})
}

func (a clientsetPodAdapter) List(ctx context.Context, ns string) ([]corev1.Pod, error) {
	l, err := a.cs.CoreV1().Pods(ns).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, err
	}
	return l.Items, nil
}

func (a clientsetPodAdapter) Delete(ctx context.Context, ns, name string, grace int64) error {
	opts := metav1.DeleteOptions{}
	if grace >= 0 {
		opts.GracePeriodSeconds = &grace
	}
	return a.cs.CoreV1().Pods(ns).Delete(ctx, name, opts)
}

func (a clientsetPodAdapter) Patch(ctx context.Context, ns, name string, patch []byte) error {
	_, err := a.cs.CoreV1().Pods(ns).Patch(ctx, name, types.StrategicMergePatchType, patch, metav1.PatchOptions{})
	return err
}
