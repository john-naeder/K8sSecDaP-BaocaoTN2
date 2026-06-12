#!/usr/bin/env python3
"""Sinh stream sự kiện mạng tổng hợp cho demo phát hiện port-scan.

Tinh giản từ scripts/generate_synthetic_events.py của stage-2: chỉ giữ
phần tạo lưu lượng nền vi-dịch-vụ + chèn một đợt port-scan từ một địa
chỉ tấn công cố định. Các phần tạo SCC / data-exfiltration / graph
edges (thuộc về môn 2) đã được loại bỏ.

Cách chạy:
    python3 scripts/generate_portscan_stream.py \
        --count 200 --attack-start 100 --scan-probes 50 --seed 42 \
        > output/portscan/sample_stream.jsonl
"""
import argparse
import json
import random
import sys

# ─── Topology giả lập (subnet 10.0.X.Y, không phụ thuộc cluster K8s) ───────

PODS = {
    "frontend-1":  "10.0.1.10",
    "frontend-2":  "10.0.1.11",
    "frontend-3":  "10.0.1.12",
    "backend-1":   "10.0.2.10",
    "backend-2":   "10.0.2.11",
    "backend-3":   "10.0.2.12",
    "postgres":    "10.0.3.10",
    "redis":       "10.0.3.11",
    "prometheus":  "10.0.4.10",
    "grafana":     "10.0.4.11",
    "attacker":    "10.0.5.99",
}

NORMAL_FLOWS = [
    ("frontend-1", "backend-1", 8080),
    ("frontend-1", "backend-2", 8080),
    ("frontend-2", "backend-1", 8080),
    ("frontend-2", "backend-3", 8080),
    ("frontend-3", "backend-2", 8080),
    ("frontend-3", "backend-3", 8080),
    ("backend-1",  "postgres",  5432),
    ("backend-2",  "postgres",  5432),
    ("backend-3",  "postgres",  5432),
    ("backend-1",  "redis",     6379),
    ("backend-2",  "redis",     6379),
    ("prometheus", "frontend-1", 9090),
    ("prometheus", "backend-1",  9090),
    ("grafana",    "prometheus", 3000),
]

COMMON_PORTS = [22, 80, 443, 3306, 5432, 6379, 8080, 8443, 9090, 27017]


def normal_event(rng, ts_ns):
    src, dst, port = rng.choice(NORMAL_FLOWS)
    return {
        "src_ip":       PODS[src],
        "dst_ip":       PODS[dst],
        "dst_port":     port,
        "timestamp_ns": ts_ns,
    }


def scan_event(rng, ts_ns):
    target_ip = f"10.0.{rng.randint(1, 4)}.{rng.randint(1, 254)}"
    return {
        "src_ip":       PODS["attacker"],
        "dst_ip":       target_ip,
        "dst_port":     rng.choice(COMMON_PORTS),
        "timestamp_ns": ts_ns,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--count",        type=int, default=200,
                    help="Tổng số sự kiện sinh ra (mặc định 200)")
    ap.add_argument("--attack-start", type=int, default=100,
                    help="Index sự kiện bắt đầu chèn quét (mặc định 100)")
    ap.add_argument("--scan-probes",  type=int, default=50,
                    help="Số gói SYN của attacker (mặc định 50)")
    ap.add_argument("--seed",         type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    base_ns = 1_700_000_000_000_000_000  # ~2023-11-15 UTC, đủ tránh bị cửa sổ trượt cắt

    events = []
    # Pha 1: lưu lượng nền trước khi tấn công (1ms / sự kiện).
    for i in range(args.attack_start):
        ts = base_ns + i * 1_000_000
        events.append(normal_event(rng, ts))

    # Pha 2: lưu lượng nền + đợt quét chen vào (attacker phát 0.1ms/probe).
    attack_ts = base_ns + args.attack_start * 1_000_000
    for j in range(args.scan_probes):
        events.append(scan_event(rng, attack_ts + j * 100_000))

    # Pha 3: tiếp tục lưu lượng nền sau đợt quét.
    after_start = attack_ts + args.scan_probes * 100_000 + 1_000_000
    remaining = args.count - args.attack_start - args.scan_probes
    for i in range(max(0, remaining)):
        ts = after_start + i * 1_000_000
        events.append(normal_event(rng, ts))

    events.sort(key=lambda e: e["timestamp_ns"])

    for e in events:
        sys.stdout.write(json.dumps(e) + "\n")

    print(f"Sinh được {len(events)} sự kiện "
          f"(nền={args.count - args.scan_probes}, "
          f"quét={args.scan_probes}, "
          f"attacker={PODS['attacker']})", file=sys.stderr)


if __name__ == "__main__":
    main()
