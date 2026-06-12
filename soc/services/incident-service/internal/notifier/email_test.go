package notifier

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

func TestSMTPNotifier_DisabledNoOp(t *testing.T) {
	n, err := NewSMTP(Config{}) // Host empty => disabled
	if err != nil {
		t.Fatal(err)
	}
	if !n.cfg.Disabled() {
		t.Fatalf("expected Disabled()=true with empty host")
	}
	err = n.NotifyIncident(context.Background(), &store.Incident{ID: 1}, "")
	if err != nil {
		t.Fatalf("disabled notifier should swallow send: %v", err)
	}
}

func TestSMTPNotifier_RendersTemplate(t *testing.T) {
	n, err := NewSMTP(Config{
		Host: "mailhog.soc.svc", Port: "1025",
		From: "zt-soc@cluster.local", To: "admin@example.com",
		ConsoleURL: "http://web-console.soc.svc:5000",
	})
	if err != nil {
		t.Fatal(err)
	}
	inc := &store.Incident{
		ID:        42,
		Type:      "port_scan",
		SourceIP:  "10.244.1.99",
		Severity:  "high",
		Status:    "new",
		CreatedAt: time.Now(),
	}
	// Render via internal template (sidesteps SMTP dial).
	var rendered strings.Builder
	if err := n.tmpl.Execute(&rendered, map[string]any{
		"Incident":    inc,
		"ConsoleURL":  "http://web-console.soc.svc:5000/incidents/42",
		"GeneratedAt": "2026-05-16T00:00:00Z",
	}); err != nil {
		t.Fatalf("render: %v", err)
	}
	body := rendered.String()
	for _, want := range []string{
		"port_scan",
		"10.244.1.99",
		"http://web-console.soc.svc:5000/incidents/42",
		"Open in admin console",
	} {
		if !strings.Contains(body, want) {
			t.Errorf("template missing %q\n---\n%s", want, body)
		}
	}
}

func TestSplitRecipients(t *testing.T) {
	got := splitRecipients(" admin@x.com , ops@y.com,, ")
	want := []string{"admin@x.com", "ops@y.com"}
	if len(got) != len(want) {
		t.Fatalf("got %v", got)
	}
	for i, g := range got {
		if g != want[i] {
			t.Errorf("[%d] got %q want %q", i, g, want[i])
		}
	}
}
