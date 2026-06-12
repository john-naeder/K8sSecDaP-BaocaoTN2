// Package notifier sends incident alerts to administrators by email.
//
// Transport: plain SMTP (net/smtp). Default target is MailHog deployed
// in-cluster (mailhog.soc.svc:1025) which accepts unauthenticated mail
// and exposes a UI for inspection. To use a real provider (Gmail, etc.)
// set SMTP_HOST + SMTP_PORT + SMTP_USERNAME + SMTP_PASSWORD env vars.
package notifier

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"html/template"
	"log"
	"net/smtp"
	"strings"
	"time"

	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

// Notifier dispatches incident events. Implementations must be safe for
// concurrent use; SMTPNotifier blocks per-call but the calling subscriber
// fans out per-message goroutines.
type Notifier interface {
	NotifyIncident(ctx context.Context, inc *store.Incident, consoleURL string) error
}

// Config groups SMTP credentials. Empty Host disables sending — useful in
// local dev or unit tests where we still want to exercise the rendering
// path without dialing.
type Config struct {
	Host       string // e.g. mailhog.soc.svc.cluster.local
	Port       string // e.g. 1025
	From       string // e.g. zt-soc@cluster.local
	To         string // comma-separated recipients
	Username   string // empty => no auth (MailHog)
	Password   string
	ConsoleURL string // base URL of the admin console for "view incident" links
}

func (c Config) Disabled() bool { return strings.TrimSpace(c.Host) == "" }

// SMTPNotifier dispatches via plain SMTP. Auth is used only when both
// Username and Password are non-empty.
type SMTPNotifier struct {
	cfg  Config
	tmpl *template.Template
}

func NewSMTP(cfg Config) (*SMTPNotifier, error) {
	if cfg.Disabled() {
		log.Printf("[notifier] SMTP disabled (no host) — emails will be dropped")
	}
	if cfg.Port == "" {
		cfg.Port = "1025"
	}
	if cfg.From == "" {
		cfg.From = "zt-soc@cluster.local"
	}
	tmpl, err := template.New("incident").Parse(htmlBody)
	if err != nil {
		return nil, fmt.Errorf("parse template: %w", err)
	}
	return &SMTPNotifier{cfg: cfg, tmpl: tmpl}, nil
}

func (n *SMTPNotifier) NotifyIncident(_ context.Context, inc *store.Incident, consoleURL string) error {
	if n.cfg.Disabled() {
		return nil
	}
	if inc == nil {
		return errors.New("nil incident")
	}
	if consoleURL == "" {
		consoleURL = n.cfg.ConsoleURL
	}
	subject := fmt.Sprintf("[ZT-SOC][%s] %s detected from %s",
		strings.ToUpper(inc.Severity), inc.Type, inc.SourceIP)

	var body bytes.Buffer
	if err := n.tmpl.Execute(&body, map[string]any{
		"Incident":    inc,
		"ConsoleURL":  fmt.Sprintf("%s/incidents/%d", strings.TrimRight(consoleURL, "/"), inc.ID),
		"GeneratedAt": time.Now().UTC().Format(time.RFC3339),
	}); err != nil {
		return fmt.Errorf("render: %w", err)
	}

	msg := buildMessage(n.cfg.From, n.cfg.To, subject, body.String())
	addr := n.cfg.Host + ":" + n.cfg.Port
	var auth smtp.Auth
	if n.cfg.Username != "" && n.cfg.Password != "" {
		auth = smtp.PlainAuth("", n.cfg.Username, n.cfg.Password, n.cfg.Host)
	}
	rcpts := splitRecipients(n.cfg.To)
	if len(rcpts) == 0 {
		return errors.New("notifier: no recipients configured")
	}
	if err := smtp.SendMail(addr, auth, n.cfg.From, rcpts, []byte(msg)); err != nil {
		return fmt.Errorf("smtp send to %s: %w", addr, err)
	}
	return nil
}

func buildMessage(from, to, subject, htmlBody string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "From: %s\r\n", from)
	fmt.Fprintf(&b, "To: %s\r\n", to)
	fmt.Fprintf(&b, "Subject: %s\r\n", subject)
	fmt.Fprintf(&b, "Date: %s\r\n", time.Now().UTC().Format(time.RFC1123Z))
	fmt.Fprintln(&b, "MIME-Version: 1.0")
	fmt.Fprintln(&b, "Content-Type: text/html; charset=UTF-8")
	fmt.Fprintln(&b)
	fmt.Fprint(&b, htmlBody)
	return b.String()
}

func splitRecipients(s string) []string {
	parts := strings.Split(s, ",")
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		if t := strings.TrimSpace(p); t != "" {
			out = append(out, t)
		}
	}
	return out
}

const htmlBody = `<html><body style="font-family: Helvetica, Arial, sans-serif; color: #222; max-width: 600px;">
  <h2 style="color: #c0392b;">ZT-SOC: New incident</h2>
  <table cellpadding="6" style="border-collapse: collapse;">
    <tr><td><b>ID</b></td><td>{{.Incident.ID}}</td></tr>
    <tr><td><b>Type</b></td><td>{{.Incident.Type}}</td></tr>
    <tr><td><b>Severity</b></td><td>{{.Incident.Severity}}</td></tr>
    <tr><td><b>Source IP</b></td><td><code>{{.Incident.SourceIP}}</code></td></tr>
    <tr><td><b>Status</b></td><td>{{.Incident.Status}}</td></tr>
    <tr><td><b>First seen</b></td><td>{{.Incident.CreatedAt.Format "2006-01-02 15:04:05 MST"}}</td></tr>
  </table>
  <p style="margin-top: 24px;">
    <a href="{{.ConsoleURL}}"
       style="background:#2c3e50;color:#fff;padding:10px 20px;text-decoration:none;border-radius:4px;display:inline-block;">
       Open in admin console
    </a>
  </p>
  <p style="color:#888;font-size:12px;margin-top:32px;">Sent by zt-soc/incident-service at {{.GeneratedAt}}.</p>
</body></html>`
