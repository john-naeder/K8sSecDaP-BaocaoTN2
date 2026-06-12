// Package natssub subscribes to enriched alerts from the aggregator
// and feeds them into the incident store + auto-action generator.
package natssub

import (
	"context"
	"encoding/json"
	"errors"
	"log"

	"github.com/johnnaeder/zt-soc/incident-service/internal/audit"
	"github.com/johnnaeder/zt-soc/incident-service/internal/notifier"
	"github.com/johnnaeder/zt-soc/incident-service/internal/response"
	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
	"github.com/nats-io/nats.go"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"
)

var (
	metricReceived = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_incident_alerts_received_total", Help: "Alerts consumed from NATS.",
	})
	metricCreated = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_incidents_created_total", Help: "New incidents inserted.",
	})
	metricUpdated = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_incidents_updated_total", Help: "Existing incidents updated by recurring alert.",
	})
	metricActions = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_actions_generated_total", Help: "NetworkPolicy drafts auto-generated.",
	})
	metricErr = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_incident_subscriber_errors_total", Help: "Subscriber-side errors.",
	})
)

type Config struct {
	URL         string
	Subject     string
	QueueGroup  string
	AutoActOn   []string // severities that trigger auto NetworkPolicy draft
}

// PodResolver maps a source IP to the Pod that held it, for K8s-context
// enrichment of incidents. Satisfied by *response.PodOps. Pass nil to disable.
type PodResolver interface {
	LookupPodByIP(ctx context.Context, namespace, ip string) (name string, found bool, err error)
}

func defaultAutoActOn() []string { return []string{"high", "critical"} }

// Run blocks until ctx is cancelled. NATS reconnects are handled internally.
// notif is optional — pass nil to skip email notifications. resolver is
// optional — pass nil to skip src_ip→Pod enrichment.
func Run(ctx context.Context, cfg Config, st *store.Store, aud *audit.Recorder, notif notifier.Notifier, resolver PodResolver) error {
	if cfg.URL == "" {
		return errors.New("nats URL required")
	}
	if cfg.Subject == "" {
		cfg.Subject = "zt.alerts.enriched"
	}
	if cfg.QueueGroup == "" {
		cfg.QueueGroup = "incident-service"
	}
	if len(cfg.AutoActOn) == 0 {
		cfg.AutoActOn = defaultAutoActOn()
	}
	autoSet := map[string]struct{}{}
	for _, s := range cfg.AutoActOn {
		autoSet[s] = struct{}{}
	}

	nc, err := nats.Connect(cfg.URL,
		nats.Name("zt-incident-service"),
		nats.MaxReconnects(-1),
	)
	if err != nil {
		return err
	}
	defer nc.Drain() //nolint:errcheck

	sub, err := nc.QueueSubscribe(cfg.Subject, cfg.QueueGroup, func(msg *nats.Msg) {
		metricReceived.Inc()
		ctx := context.Background()

		var inAlert store.IncomingAlert
		if err := json.Unmarshal(msg.Data, &inAlert); err != nil {
			metricErr.Inc()
			log.Printf("[sub] bad payload: %v", err)
			return
		}

		inc, created, err := st.UpsertIncident(ctx, &inAlert)
		if err != nil {
			metricErr.Inc()
			log.Printf("[sub] upsert: %v", err)
			return
		}
		if err := st.AppendAlert(ctx, inc.ID, json.RawMessage(msg.Data)); err != nil {
			log.Printf("[sub] append alert: %v", err)
		}
		if created {
			metricCreated.Inc()
			aud.Record(ctx, "system", "system", "incident.created", "incident", inc.ID, map[string]any{
				"type":     inc.Type,
				"source":   inc.SourceIP,
				"severity": inc.Severity,
			})
			if notif != nil {
				if err := notif.NotifyIncident(ctx, inc, ""); err != nil {
					log.Printf("[sub] notify: %v", err)
				}
			}
		} else {
			metricUpdated.Inc()
		}

		// Best-effort K8s enrichment: which Pod held the offending source IP.
		if created && resolver != nil {
			ns := response.DefaultQuarantineNamespace
			if name, found, rerr := resolver.LookupPodByIP(ctx, ns, inc.SourceIP); rerr != nil {
				log.Printf("[sub] resolve pod for %s: %v", inc.SourceIP, rerr)
			} else if found {
				if err := st.SetIncidentPodContext(ctx, inc.ID, name, ns); err != nil {
					log.Printf("[sub] set pod context inc=%d: %v", inc.ID, err)
				}
			}
		}

		// Auto-action when severity matches threshold AND no pending action exists.
		if _, ok := autoSet[inc.Severity]; ok {
			actions, err := st.ListActions(ctx, "pending", 200)
			pendingForThisIncident := false
			if err == nil {
				for _, a := range actions {
					if a.IncidentID == inc.ID {
						pendingForThisIncident = true
						break
					}
				}
			}
			if !pendingForThisIncident {
				yaml := response.GenerateQuarantine(response.QuarantineRequest{
					IncidentID: inc.ID,
					SourceIP:   inc.SourceIP,
					Severity:   inc.Severity,
					Reason:     inc.Type,
				})
				act := &store.Action{
					IncidentID: inc.ID,
					Type:       "quarantine_pod",
					YAMLDraft:  yaml,
				}
				if err := st.CreateAction(ctx, act); err == nil {
					metricActions.Inc()
					aud.Record(ctx, "system", "system", "action.auto_generated", "action", act.ID, map[string]any{
						"incident_id": inc.ID,
						"severity":    inc.Severity,
					})
				} else {
					log.Printf("[sub] create action: %v", err)
				}
			}
		}
	})
	if err != nil {
		return err
	}
	defer sub.Unsubscribe() //nolint:errcheck

	log.Printf("[sub] listening on %s queue=%s", cfg.Subject, cfg.QueueGroup)
	<-ctx.Done()
	return nil
}
