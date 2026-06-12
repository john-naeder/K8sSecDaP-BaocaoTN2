// soc-incident-service — REST API for SOC incident lifecycle.
//
// Subscribes to enriched alerts on NATS, persists them as incidents in
// Postgres, and exposes a small REST API for triage / response actions.
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/johnnaeder/zt-soc/incident-service/internal/api"
	"github.com/johnnaeder/zt-soc/incident-service/internal/audit"
	"github.com/johnnaeder/zt-soc/incident-service/internal/natssub"
	"github.com/johnnaeder/zt-soc/incident-service/internal/notifier"
	"github.com/johnnaeder/zt-soc/incident-service/internal/rbac"
	"github.com/johnnaeder/zt-soc/incident-service/internal/response"
	"github.com/johnnaeder/zt-soc/incident-service/internal/storage"
	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

var (
	dsn         = flag.String("dsn",          getenv("DATABASE_URL", "postgres://soc:soc@postgres.soc:5432/soc?sslmode=disable"), "Postgres DSN")
	natsURL     = flag.String("nats-url",     getenv("NATS_URL",     "nats://nats.soc.svc.cluster.local:4222"),                  "NATS URL")
	natsSubject = flag.String("nats-subject", getenv("NATS_SUBJECT", "zt.alerts.enriched"),                                       "NATS subject")
	listenAddr  = flag.String("listen",       getenv("LISTEN_ADDR",  ":8080"),                                                    "API listen address")
	metricsAddr = flag.String("metrics",      getenv("METRICS_ADDR", ":9101"),                                                    "Prometheus listen address")
	tokensEnv   = flag.String("tokens",       os.Getenv("RBAC_TOKENS"),                                                            "RBAC token map (user:role:token,...)")
	applyMode   = flag.String("apply-mode",   getenv("APPLY_MODE",   "dryrun"),                                                    "dryrun|apply")
	draftDir    = flag.String("draft-dir",    getenv("DRAFT_DIR",    "/var/lib/zt/netpol-drafts"),                                "where dryrun YAMLs go")

	// Email / web-console wiring (see internal/notifier).
	smtpHost    = flag.String("smtp-host",    getenv("SMTP_HOST",    ""),                                                          "SMTP host (empty = disabled)")
	smtpPort    = flag.String("smtp-port",    getenv("SMTP_PORT",    "1025"),                                                      "SMTP port")
	smtpFrom    = flag.String("smtp-from",    getenv("SMTP_FROM",    "zt-soc@cluster.local"),                                      "SMTP envelope-from")
	smtpTo      = flag.String("smtp-to",      getenv("SMTP_TO",      ""),                                                          "comma-separated admin recipients")
	smtpUser    = flag.String("smtp-user",    getenv("SMTP_USERNAME", ""),                                                          "SMTP username (empty = no auth)")
	smtpPass    = flag.String("smtp-pass",    os.Getenv("SMTP_PASSWORD"),                                                            "SMTP password")
	consoleURL  = flag.String("console-url",  getenv("WEB_CONSOLE_URL", "http://web-console.soc.svc:5000"),                          "base URL of the admin web console")

	// MinIO sink for weekly report uploads (Phase D).
	minioEndpoint = flag.String("minio-endpoint", getenv("MINIO_ENDPOINT", ""),                  "MinIO endpoint host:port (empty = disabled)")
	minioAccess   = flag.String("minio-access",   getenv("MINIO_ACCESS_KEY", ""),                "MinIO access key")
	minioSecret   = flag.String("minio-secret",   os.Getenv("MINIO_SECRET_KEY"),                 "MinIO secret key")
	minioSecure   = flag.Bool  ("minio-secure",   os.Getenv("MINIO_SECURE") == "true",          "MinIO TLS")
	minioRegion   = flag.String("minio-region",   getenv("MINIO_REGION",     "us-east-1"),       "MinIO region")
)

func main() {
	flag.Parse()
	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	log.Printf("[svc] starting: dsn=%s nats=%s subject=%s listen=%s",
		redactDSN(*dsn), *natsURL, *natsSubject, *listenAddr)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	st, err := connectWithRetry(ctx, *dsn, 30)
	if err != nil {
		log.Fatalf("[svc] postgres: %v", err)
	}
	defer st.Close()

	aud := &audit.Recorder{Store: st}

	applier, err := response.NewApplier(*applyMode, *draftDir)
	if err != nil {
		log.Fatalf("[svc] applier: %v", err)
	}

	// PodOps wires CoreV1().Pods(...) for delete_pod / shutdown_pod actions.
	// In dryrun mode (no in-cluster config available) we skip wiring and the
	// API will reject those action types until the cluster is reachable.
	var podOps *response.PodOps
	if *applyMode == "apply" {
		podOps, err = response.NewPodOps()
		if err != nil {
			log.Fatalf("[svc] podops: %v", err)
		}
	} else {
		log.Printf("[svc] APPLY_MODE=dryrun — pod operations disabled")
	}

	notif, err := notifier.NewSMTP(notifier.Config{
		Host: *smtpHost, Port: *smtpPort,
		From: *smtpFrom, To: *smtpTo,
		Username: *smtpUser, Password: *smtpPass,
		ConsoleURL: *consoleURL,
	})
	if err != nil {
		log.Fatalf("[svc] notifier: %v", err)
	}

	var reportSink api.ReportSink
	if *minioEndpoint != "" {
		mc, err := storage.NewMinIO(storage.Config{
			Endpoint: *minioEndpoint, AccessKey: *minioAccess, SecretKey: *minioSecret,
			Secure: *minioSecure, Region: *minioRegion,
		})
		if err != nil {
			log.Fatalf("[svc] minio: %v", err)
		}
		reportSink = mc
		log.Printf("[svc] MinIO report sink enabled (endpoint=%s)", *minioEndpoint)
	}

	srv := &api.Server{Store: st, Audit: aud, Applier: applier, PodOps: podOps, ReportSink: reportSink}
	tokens := rbac.ParseTokens(*tokensEnv)
	if len(tokens) == 0 {
		log.Printf("[svc] WARN: RBAC_TOKENS empty — every request is anonymous viewer")
	} else {
		log.Printf("[svc] RBAC enabled with %d tokens", len(tokens))
	}

	apiHandler := srv.Routes(tokens)

	httpSrv := &http.Server{
		Addr:              *listenAddr,
		Handler:           apiHandler,
		ReadHeaderTimeout: 5 * time.Second,
	}
	go func() {
		log.Printf("[svc] api listening on %s", *listenAddr)
		if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[svc] api: %v", err)
		}
	}()

	metricsSrv := &http.Server{
		Addr:              *metricsAddr,
		Handler:           promMux(),
		ReadHeaderTimeout: 5 * time.Second,
	}
	go func() {
		log.Printf("[svc] metrics listening on %s", *metricsAddr)
		if err := metricsSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[svc] metrics: %v", err)
		}
	}()

	// podOps (when wired in apply mode) doubles as the src_ip→Pod resolver for
	// incident enrichment. Assign to a typed-nil-safe interface var.
	var resolver natssub.PodResolver
	if podOps != nil {
		resolver = podOps
	}

	subCtx, subCancel := context.WithCancel(ctx)
	go func() {
		if err := natssub.Run(subCtx, natssub.Config{
			URL:     *natsURL,
			Subject: *natsSubject,
		}, st, aud, notif, resolver); err != nil {
			log.Printf("[svc] nats subscriber exited: %v", err)
		}
	}()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	log.Printf("[svc] shutting down")
	subCancel()
	shutCtx, shutCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer shutCancel()
	httpSrv.Shutdown(shutCtx)    //nolint:errcheck
	metricsSrv.Shutdown(shutCtx) //nolint:errcheck
}

func promMux() *http.ServeMux {
	m := http.NewServeMux()
	m.Handle("/metrics", promhttp.Handler())
	return m
}

func getenv(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func redactDSN(s string) string {
	// pgx DSN format: postgres://user:pass@host/db
	for i := 0; i < len(s)-1; i++ {
		if s[i] == ':' && i+2 < len(s) && s[i+1] == '/' && s[i+2] == '/' {
			at := -1
			for j := i + 3; j < len(s); j++ {
				if s[j] == '@' {
					at = j
					break
				}
			}
			if at > 0 {
				return s[:i+3] + "***" + s[at:]
			}
		}
	}
	return s
}

func connectWithRetry(ctx context.Context, dsn string, maxAttempts int) (*store.Store, error) {
	var lastErr error
	for i := 0; i < maxAttempts; i++ {
		st, err := store.New(ctx, dsn)
		if err == nil {
			return st, nil
		}
		lastErr = err
		log.Printf("[svc] postgres connect attempt %d/%d: %v", i+1, maxAttempts, err)
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-time.After(2 * time.Second):
		}
	}
	return nil, fmt.Errorf("after %d attempts: %w", maxAttempts, lastErr)
}
