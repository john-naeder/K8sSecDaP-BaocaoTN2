// alert-bridge — tails an ndjson alerts file and re-publishes each line
// to NATS. Runs as a sidecar in the pipeline pod so the C++ pipeline does
// not need to link libnats.
package main

import (
	"bufio"
	"context"
	"flag"
	"io"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/nats-io/nats.go"
)

var (
	natsURL = flag.String("nats-url", getenv("NATS_URL", "nats://nats.soc.svc.cluster.local:4222"), "NATS URL")
	subject = flag.String("subject",  getenv("SUBJECT",  "zt.alerts.raw"),                          "publish subject")
	source  = flag.String("source",   getenv("ALERTS_FILE", "/var/log/zt/alerts.json"),             "ndjson file to tail")
)

func main() {
	flag.Parse()
	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	log.Printf("[bridge] tail=%s subject=%s nats=%s", *source, *subject, *natsURL)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	go func() { <-sig; cancel() }()

	// Reconnect-forever NATS connection.
	nc, err := nats.Connect(*natsURL,
		nats.Name("zt-alert-bridge"),
		nats.MaxReconnects(-1),
		nats.ReconnectWait(2*time.Second),
		nats.DisconnectErrHandler(func(_ *nats.Conn, err error) { log.Printf("[bridge] nats disconnect: %v", err) }),
		nats.ReconnectHandler(func(c *nats.Conn) { log.Printf("[bridge] nats reconnect %s", c.ConnectedUrl()) }),
	)
	if err != nil {
		log.Fatalf("[bridge] nats connect: %v", err)
	}
	defer nc.Drain() //nolint:errcheck

	if err := tailAndPublish(ctx, *source, nc, *subject); err != nil {
		log.Fatalf("[bridge] tail: %v", err)
	}
}

// tailAndPublish opens the file, seeks to end, and re-publishes every newly
// appended line. If the file is rotated/truncated the loop reopens.
func tailAndPublish(ctx context.Context, path string, nc *nats.Conn, subject string) error {
	for {
		if err := readForever(ctx, path, nc, subject); err != nil {
			if ctx.Err() != nil {
				return nil
			}
			log.Printf("[bridge] reopening %s after error: %v", path, err)
		}
		if ctx.Err() != nil {
			return nil
		}
		time.Sleep(2 * time.Second)
	}
}

func readForever(ctx context.Context, path string, nc *nats.Conn, subject string) error {
	f, err := waitForFile(ctx, path)
	if err != nil {
		return err
	}
	defer f.Close()

	if _, err := f.Seek(0, io.SeekEnd); err != nil {
		return err
	}
	r := bufio.NewReader(f)
	for {
		if ctx.Err() != nil {
			return nil
		}
		line, err := r.ReadBytes('\n')
		if err == io.EOF {
			// no new data — sleep a bit, then check inode rotation
			time.Sleep(500 * time.Millisecond)
			if rotated(f, path) {
				return nil
			}
			continue
		}
		if err != nil {
			return err
		}
		line = trimTrailingNewline(line)
		if len(line) == 0 || line[0] != '{' {
			continue
		}
		if err := nc.Publish(subject, line); err != nil {
			log.Printf("[bridge] publish err: %v", err)
			continue
		}
	}
}

func waitForFile(ctx context.Context, path string) (*os.File, error) {
	for {
		f, err := os.Open(path)
		if err == nil {
			return f, nil
		}
		if !os.IsNotExist(err) {
			return nil, err
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-time.After(time.Second):
		}
	}
}

func rotated(f *os.File, path string) bool {
	st1, err1 := f.Stat()
	st2, err2 := os.Stat(path)
	if err1 != nil || err2 != nil {
		return true
	}
	return !os.SameFile(st1, st2)
}

func trimTrailingNewline(b []byte) []byte {
	for len(b) > 0 && (b[len(b)-1] == '\n' || b[len(b)-1] == '\r') {
		b = b[:len(b)-1]
	}
	return b
}

func getenv(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}
