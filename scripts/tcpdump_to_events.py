#!/usr/bin/env python3
"""tcpdump → NetworkEvent JSON converter.

Reads tcpdump output (line-based, `-l` for line buffering) on stdin and emits
one JSON event per TCP SYN seen. Designed to be piped into zt-pipeline:

    tcpdump -nn -l -i any 'tcp[tcpflags] & tcp-syn != 0 and tcp[tcpflags] & tcp-ack == 0' \\
        | tcpdump_to_events.py | zt-pipeline --config /etc/zt/pipeline.yaml

Each tcpdump line looks roughly like:
    21:30:42.123456 IP 10.244.1.5.34522 > 10.244.2.10.80: Flags [S], seq ...
"""
import json
import os
import re
import sys
import time

# Matches the IP-flow portion of a tcpdump line (format varies with -i any:
# may include a leading "veth0  Out", and timestamp at column 0). The flow
# fragment "IP <src>.<sport> > <dst>.<dport>: Flags [S]" is what we need.
LINE_RE = re.compile(
    r"\bIP\s+"
    r"(?P<src>\d+\.\d+\.\d+\.\d+)\.(?P<sport>\d+)\s+>\s+"
    r"(?P<dst>\d+\.\d+\.\d+\.\d+)\.(?P<dport>\d+):\s+Flags\s+\[S\]"
)


def main() -> int:
    pid = os.getpid()
    sys.stdout.reconfigure(line_buffering=True)
    for line in sys.stdin:
        m = LINE_RE.search(line)
        if not m:
            continue
        evt = {
            "src_ip":       m.group("src"),
            "dst_ip":       m.group("dst"),
            "dst_port":     int(m.group("dport")),
            "pid":          pid,
            "timestamp_ns": time.time_ns(),
        }
        sys.stdout.write(json.dumps(evt))
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
