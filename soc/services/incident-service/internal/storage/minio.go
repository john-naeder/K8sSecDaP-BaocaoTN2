// Package storage uploads rendered report artifacts to MinIO. The
// incident-service treats the sink as optional: when MINIO_ENDPOINT
// is empty, Server.ReportSink stays nil and the upload branch in
// generateAndStoreWeeklyReport is skipped.
package storage

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

type Config struct {
	Endpoint  string // e.g. minio.soc.svc.cluster.local:9000
	AccessKey string
	SecretKey string
	Secure    bool // false for in-cluster plaintext
	Region    string
}

func (c Config) Disabled() bool { return strings.TrimSpace(c.Endpoint) == "" }

type MinIOSink struct {
	cli *minio.Client
}

func NewMinIO(cfg Config) (*MinIOSink, error) {
	if cfg.Disabled() {
		return nil, errors.New("storage: MINIO_ENDPOINT empty")
	}
	cli, err := minio.New(cfg.Endpoint, &minio.Options{
		Creds:  credentials.NewStaticV4(cfg.AccessKey, cfg.SecretKey, ""),
		Secure: cfg.Secure,
		Region: cfg.Region,
	})
	if err != nil {
		return nil, fmt.Errorf("minio client: %w", err)
	}
	return &MinIOSink{cli: cli}, nil
}

// UploadHTML stores the body under bucket/key. The bucket must already
// exist — initContainer in deploy/data/15-minio.yaml provisions the four
// expected buckets at MinIO startup.
func (s *MinIOSink) UploadHTML(ctx context.Context, bucket, key, body string) (string, error) {
	r := bytes.NewReader([]byte(body))
	_, err := s.cli.PutObject(ctx, bucket, key, r, int64(len(body)),
		minio.PutObjectOptions{
			ContentType:  "text/html; charset=utf-8",
			CacheControl: "no-store",
			UserMetadata: map[string]string{
				"x-zt-source":      "incident-service",
				"x-zt-uploaded-at": time.Now().UTC().Format(time.RFC3339),
			},
		})
	if err != nil {
		return "", fmt.Errorf("put %s/%s: %w", bucket, key, err)
	}
	scheme := "http"
	if cfg := s.cli.EndpointURL(); cfg != nil {
		scheme = cfg.Scheme
	}
	return fmt.Sprintf("%s://%s/%s/%s", scheme, s.cli.EndpointURL().Host, bucket, key), nil
}
