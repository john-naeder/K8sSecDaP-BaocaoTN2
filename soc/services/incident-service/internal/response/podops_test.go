package response

import (
	"context"
	"strings"
	"testing"

	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes/fake"
)

func newPod(ns, name string) *corev1.Pod {
	return &corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{Namespace: ns, Name: name, Labels: map[string]string{"app": "attacker"}},
	}
}

func TestPodOps_DeletePod_Success(t *testing.T) {
	cs := fake.NewSimpleClientset(newPod("zt-targets", "attacker"))
	po := NewPodOpsFromClientset(cs)

	if err := po.DeletePod(context.Background(), "zt-targets", "attacker"); err != nil {
		t.Fatalf("DeletePod: %v", err)
	}
	_, err := cs.CoreV1().Pods("zt-targets").Get(context.Background(), "attacker", metav1.GetOptions{})
	if err == nil {
		t.Fatalf("expected pod gone, still present")
	}
}

func TestPodOps_DeletePod_NotFound(t *testing.T) {
	cs := fake.NewSimpleClientset()
	po := NewPodOpsFromClientset(cs)
	err := po.DeletePod(context.Background(), "zt-targets", "ghost")
	if err == nil || !strings.Contains(err.Error(), "not found") {
		t.Fatalf("expected not-found error, got %v", err)
	}
}

func TestPodOps_ShutdownPod_LabelsAndDeletes(t *testing.T) {
	cs := fake.NewSimpleClientset(newPod("zt-targets", "attacker"))
	po := NewPodOpsFromClientset(cs)

	if err := po.ShutdownPod(context.Background(), "zt-targets", "attacker"); err != nil {
		t.Fatalf("ShutdownPod: %v", err)
	}
	// Fake clientset removes the pod synchronously when Delete is called.
	_, err := cs.CoreV1().Pods("zt-targets").Get(context.Background(), "attacker", metav1.GetOptions{})
	if err == nil {
		t.Fatalf("expected pod gone after shutdown delete, still present")
	}
}

func TestPodOps_ShutdownPod_NotFound(t *testing.T) {
	cs := fake.NewSimpleClientset()
	po := NewPodOpsFromClientset(cs)
	err := po.ShutdownPod(context.Background(), "zt-targets", "ghost")
	if err == nil || !strings.Contains(err.Error(), "not found") {
		t.Fatalf("expected not-found error, got %v", err)
	}
}

func TestPodOps_QuarantinePodByIP_LabelsMatchingPod(t *testing.T) {
	p := newPod("zt-targets", "attacker")
	p.Status.PodIP = "10.244.1.99"
	cs := fake.NewSimpleClientset(p)
	po := NewPodOpsFromClientset(cs)

	name, err := po.QuarantinePodByIP(context.Background(), "zt-targets", "10.244.1.99", "zt-soc-quarantine-42")
	if err != nil {
		t.Fatalf("QuarantinePodByIP: %v", err)
	}
	if name != "attacker" {
		t.Fatalf("got pod %q, want attacker", name)
	}
	got, err := cs.CoreV1().Pods("zt-targets").Get(context.Background(), "attacker", metav1.GetOptions{})
	if err != nil {
		t.Fatalf("get pod: %v", err)
	}
	if got.Labels["zt-soc-quarantine-42"] != "true" {
		t.Fatalf("quarantine label not applied, labels=%v", got.Labels)
	}
}

func TestPodOps_QuarantinePodByIP_NoMatch(t *testing.T) {
	p := newPod("zt-targets", "innocent")
	p.Status.PodIP = "10.244.1.5"
	cs := fake.NewSimpleClientset(p)
	po := NewPodOpsFromClientset(cs)

	_, err := po.QuarantinePodByIP(context.Background(), "zt-targets", "10.244.9.9", "zt-soc-quarantine-1")
	if err == nil || !strings.Contains(err.Error(), "no pod with IP") {
		t.Fatalf("expected no-pod error, got %v", err)
	}
}

func TestPodOps_LookupPodByIP(t *testing.T) {
	p := newPod("zt-targets", "attacker")
	p.Status.PodIP = "10.244.1.99"
	cs := fake.NewSimpleClientset(p)
	po := NewPodOpsFromClientset(cs)

	name, found, err := po.LookupPodByIP(context.Background(), "zt-targets", "10.244.1.99")
	if err != nil || !found || name != "attacker" {
		t.Fatalf("LookupPodByIP: name=%q found=%v err=%v", name, found, err)
	}

	_, found, err = po.LookupPodByIP(context.Background(), "zt-targets", "10.244.9.9")
	if err != nil || found {
		t.Fatalf("expected not found for unknown IP, got found=%v err=%v", found, err)
	}
}
