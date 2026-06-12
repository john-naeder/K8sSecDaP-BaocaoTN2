#!/usr/bin/env python3
"""
Zero-Trust Network Mapper — Synthetic Event Generator

Generates realistic network events for testing the pipeline
without requiring eBPF or a K8s cluster.

Usage:
    python generate_synthetic_events.py [OPTIONS] > /tmp/events.json

Options:
    --count N           Total number of events (default: 10000)
    --attack-start N    Event index where attack begins (default: 7000)
    --seed N            Random seed (default: 42)
    --output PATH       Output file (default: stdout)

Scenarios generated:
    1. Normal traffic:   frontend → backend → database (allowed paths)
    2. Port scan:        attacker scans many IPs rapidly
    3. Lateral movement: compromised pod connects to database directly
    4. Architecture violation: creates cycles (frontend ↔ database)
"""

import argparse
import json
import random
import sys
import time


# ─── K8s-like Pod IPs ──────────────────────────────────────────────────────

PODS = {
    # Frontend pods (3 replicas)
    "frontend-1": "10.244.1.10",
    "frontend-2": "10.244.1.11",
    "frontend-3": "10.244.1.12",

    # Backend pods (3 replicas)
    "backend-1": "10.244.2.10",
    "backend-2": "10.244.2.11",
    "backend-3": "10.244.2.12",

    # Database pods
    "postgres":   "10.244.3.10",
    "redis":      "10.244.3.11",

    # Infrastructure
    "prometheus": "10.244.4.10",
    "grafana":    "10.244.4.11",

    # Attacker (compromised or rogue pod)
    "attacker":   "10.244.5.99",
}

# Allowed communication patterns (normal architecture)
NORMAL_FLOWS = [
    # Frontend → Backend (HTTP)
    ("frontend-1", "backend-1", 8080),
    ("frontend-1", "backend-2", 8080),
    ("frontend-2", "backend-1", 8080),
    ("frontend-2", "backend-3", 8080),
    ("frontend-3", "backend-2", 8080),
    ("frontend-3", "backend-3", 8080),

    # Backend → Database
    ("backend-1", "postgres", 5432),
    ("backend-2", "postgres", 5432),
    ("backend-3", "postgres", 5432),

    # Backend → Cache
    ("backend-1", "redis", 6379),
    ("backend-2", "redis", 6379),
    ("backend-3", "redis", 6379),

    # Monitoring (Prometheus scrapes everything)
    ("prometheus", "frontend-1", 9090),
    ("prometheus", "backend-1", 9090),
    ("prometheus", "postgres", 9090),

    # Grafana → Prometheus
    ("grafana", "prometheus", 3000),
]


def generate_event(src_name, dst_name, dst_port, timestamp_ns, pid=None):
    """Generate a single network event as a JSON dict."""
    return {
        "src_ip": PODS[src_name],
        "dst_ip": PODS[dst_name],
        "dst_port": dst_port,
        "pid": pid or random.randint(1000, 65535),
        "timestamp_ns": timestamp_ns,
    }


def generate_normal_traffic(count, start_time_ns, rng):
    """Generate normal microservice traffic following allowed patterns."""
    events = []
    for i in range(count):
        flow = rng.choice(NORMAL_FLOWS)
        src, dst, port = flow
        ts = start_time_ns + i * 1_000_000  # 1ms between events
        events.append(generate_event(src, dst, port, ts))
    return events


def generate_port_scan(target_subnet, num_probes, start_time_ns, rng):
    """
    Simulate port scanning: attacker connects to many IPs rapidly.
    CMS should detect the high frequency from attacker IP.
    """
    events = []
    attacker = "attacker"
    common_ports = [22, 80, 443, 3306, 5432, 6379, 8080, 8443, 9090, 27017]

    for i in range(num_probes):
        # Scan random IPs in the pod subnet
        target_ip = f"10.244.{rng.randint(1, 5)}.{rng.randint(1, 254)}"
        port = rng.choice(common_ports)
        ts = start_time_ns + i * 100_000  # 0.1ms between probes (fast scan)

        events.append({
            "src_ip": PODS[attacker],
            "dst_ip": target_ip,
            "dst_port": port,
            "pid": 31337,
            "timestamp_ns": ts,
        })
    return events


def generate_lateral_movement(start_time_ns):
    """
    Simulate lateral movement: compromised frontend connects directly to database.
    Tarjan should detect new SCC (frontend ↔ database).
    """
    events = []
    # Compromised frontend tries to connect to database directly (bypass backend)
    violations = [
        ("frontend-1", "postgres", 5432),   # frontend → database (violation!)
        ("frontend-1", "redis", 6379),       # frontend → cache (violation!)
        ("postgres", "frontend-1", 8080),    # database → frontend (reverse! creates cycle)
    ]

    for i, (src, dst, port) in enumerate(violations):
        ts = start_time_ns + i * 5_000_000  # 5ms between
        events.append(generate_event(src, dst, port, ts))

    return events


def generate_data_exfiltration(start_time_ns, rng):
    """
    Simulate data exfiltration: backend pod connects to external IP.
    LPM Trie should classify destination as external (label 0).
    """
    events = []
    external_ips = [
        "203.0.113.50",   # TEST-NET-3 (example external)
        "198.51.100.10",  # TEST-NET-2
        "45.33.32.156",   # External C2 server
    ]

    for i, ext_ip in enumerate(external_ips):
        ts = start_time_ns + i * 10_000_000  # 10ms between
        events.append({
            "src_ip": PODS["backend-2"],
            "dst_ip": ext_ip,
            "dst_port": 443,
            "pid": 9999,
            "timestamp_ns": ts,
        })

    return events


def main():
    parser = argparse.ArgumentParser(
        description="Generate synthetic network events for Zero-Trust Mapper testing"
    )
    parser.add_argument("--count", type=int, default=10000,
                        help="Total normal events (default: 10000)")
    parser.add_argument("--attack-start", type=int, default=7000,
                        help="Event index where attack starts (default: 7000)")
    parser.add_argument("--scan-probes", type=int, default=500,
                        help="Number of port scan probes (default: 500)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed (default: 42)")
    parser.add_argument("--output", type=str, default=None,
                        help="Output file (default: stdout)")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    base_time = 1_000_000_000_000  # Arbitrary base timestamp

    out = sys.stdout
    if args.output:
        out = open(args.output, "w")

    # Phase 1: Normal traffic (before attack)
    normal_before = generate_normal_traffic(
        args.attack_start, base_time, rng
    )

    # Phase 2: Attack begins — interleave attack events with normal traffic
    attack_start_time = base_time + args.attack_start * 1_000_000

    port_scan = generate_port_scan(
        "10.244.0.0/16", args.scan_probes, attack_start_time, rng
    )

    lateral = generate_lateral_movement(
        attack_start_time + args.scan_probes * 100_000
    )

    exfil = generate_data_exfiltration(
        attack_start_time + args.scan_probes * 100_000 + 50_000_000, rng
    )

    # Phase 3: Normal traffic continues during/after attack
    normal_after = generate_normal_traffic(
        args.count - args.attack_start,
        attack_start_time + 1_000_000_000,  # 1 second after attack
        rng
    )

    # Merge all events and sort by timestamp
    all_events = normal_before + port_scan + lateral + exfil + normal_after
    all_events.sort(key=lambda e: e["timestamp_ns"])

    # Output as JSON lines
    for evt in all_events:
        out.write(json.dumps(evt) + "\n")

    if args.output:
        out.close()
        print(f"Generated {len(all_events)} events to {args.output}",
              file=sys.stderr)
    else:
        print(f"Generated {len(all_events)} events", file=sys.stderr)

    # Summary to stderr
    print(f"\nSummary:", file=sys.stderr)
    print(f"  Normal events:    {args.count}", file=sys.stderr)
    print(f"  Port scan probes: {args.scan_probes}", file=sys.stderr)
    print(f"  Lateral movement: {len(lateral)} events", file=sys.stderr)
    print(f"  Data exfiltration: {len(exfil)} events", file=sys.stderr)
    print(f"  Total events:     {len(all_events)}", file=sys.stderr)


if __name__ == "__main__":
    main()
