// soc-aggregator — subscribes to raw alerts on NATS, deduplicates and
// enriches them with cross-node correlation, then re-publishes the
// enriched stream for the incident-service to consume.
//
// Topics:
//   in  : zt.alerts.raw         (per-node pipeline output)
//   out : zt.alerts.enriched    (consumed by S3 incident-service)
//
// Endpoints:
//   :9100/metrics  — Prometheus metrics
//   :9100/health   — liveness probe
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

var (
	natsURL     = flag.String("nats-url",     getEnv("NATS_URL",     "nats://nats.soc.svc.cluster.local:4222"), "NATS server URL")
	subjectIn   = flag.String("subject-in",   getEnv("SUBJECT_IN",   "zt.alerts.raw"),       "subject to subscribe")
	subjectOut  = flag.String("subject-out",  getEnv("SUBJECT_OUT",  "zt.alerts.enriched"),  "subject to publish enriched alerts")
	queueGroup  = flag.String("queue-group",  getEnv("QUEUE_GROUP",  "aggregator"),          "NATS queue group (HA)")
	listenAddr  = flag.String("listen",       getEnv("LISTEN_ADDR",  ":9100"),               "HTTP metrics/health listen address")
	windowSecs  = flag.Int(   "window-secs",  getEnvInt("WINDOW_SECS", 60),                  "dedup window in seconds")
)

var (
	metricReceived = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_aggregator_alerts_received_total",
		Help: "Total raw alerts consumed from NATS.",
	})
	metricEmitted = promauto.NewCounterVec(prometheus.CounterOpts{
		Name: "zt_aggregator_alerts_emitted_total",
		Help: "Total enriched alerts published, labelled by reason.",
	}, []string{"reason"})
	metricSuppressed = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_aggregator_alerts_suppressed_total",
		Help: "Duplicate alerts suppressed by the correlator.",
	})
	metricActiveIncidents = promauto.NewGauge(prometheus.GaugeOpts{
		Name: "zt_aggregator_active_incidents",
		Help: "Active incidents within the dedup window.",
	})
	metricCrossNode = promauto.NewGauge(prometheus.GaugeOpts{
		Name: "zt_aggregator_active_cross_node_incidents",
		Help: "Active incidents observed on >=2 nodes.",
	})
	metricParseErr = promauto.NewCounter(prometheus.CounterOpts{
		Name: "zt_aggregator_parse_errors_total",
		Help: "Malformed messages dropped before correlator.",
	})
)

func main() {
	flag.Parse()
	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	log.Printf("[agg] starting nats=%s in=%s out=%s window=%ds", *natsURL, *subjectIn, *subjectOut, *windowSecs)

	corr := NewCorrelator(time.Duration(*windowSecs) * time.Second)
	defer corr.Stop()

	nc, err := nats.Connect(*natsURL,
		nats.Name("zt-aggregator"),
		nats.MaxReconnects(-1),
		nats.ReconnectWait(2*time.Second),
		nats.DisconnectErrHandler(func(_ *nats.Conn, err error) { log.Printf("[agg] nats disconnect: %v", err) }),
		nats.ReconnectHandler(func(c *nats.Conn) { log.Printf("[agg] nats reconnect to %s", c.ConnectedUrl()) }),
	)
	if err != nil {
		log.Fatalf("[agg] nats connect: %v", err)
	}
	defer nc.Drain() //nolint:errcheck

	sub, err := nc.QueueSubscribe(*subjectIn, *queueGroup, func(msg *nats.Msg) {
		metricReceived.Inc()
		var a Alert
		if err := json.Unmarshal(msg.Data, &a); err != nil {
			metricParseErr.Inc()
			log.Printf("[agg] parse error: %v body=%q", err, truncate(msg.Data, 120))
			return
		}
		out, emit := corr.Process(&a)
		if !emit {
			metricSuppressed.Inc()
			return
		}
		body, err := json.Marshal(out)
		if err != nil {
			log.Printf("[agg] re-marshal error: %v", err)
			return
		}
		if err := nc.Publish(*subjectOut, body); err != nil {
			log.Printf("[agg] publish error: %v", err)
			return
		}
		reason := "first_seen"
		if out.Correlation != nil && out.Correlation.CrossNode {
			reason = "cross_node_upgrade"
		}
		metricEmitted.WithLabelValues(reason).Inc()
	})
	if err != nil {
		log.Fatalf("[agg] subscribe: %v", err)
	}
	defer sub.Unsubscribe() //nolint:errcheck

	go updateGaugesLoop(corr)

	mux := http.NewServeMux()
	mux.Handle("/metrics", promhttp.Handler())
	mux.HandleFunc("/health", func(w http.ResponseWriter, _ *http.Request) {
		if !nc.IsConnected() {
			http.Error(w, "nats disconnected", http.StatusServiceUnavailable)
			return
		}
		fmt.Fprintln(w, "ok")
	})
	srv := &http.Server{Addr: *listenAddr, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	go func() {
		log.Printf("[agg] http listening on %s", *listenAddr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[agg] http: %v", err)
		}
	}()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	log.Printf("[agg] shutting down")
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	srv.Shutdown(ctx) //nolint:errcheck
}

func updateGaugesLoop(c *Correlator) {
	t := time.NewTicker(2 * time.Second)
	defer t.Stop()
	for range t.C {
		active, cross := c.Stats()
		metricActiveIncidents.Set(float64(active))
		metricCrossNode.Set(float64(cross))
	}
}

func truncate(b []byte, n int) string {
	if len(b) <= n {
		return string(b)
	}
	return string(b[:n]) + "..."
}

func getEnv(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func getEnvInt(k string, def int) int {
	if v := os.Getenv(k); v != "" {
		var n int
		if _, err := fmt.Sscanf(v, "%d", &n); err == nil {
			return n
		}
	}
	return def
}
