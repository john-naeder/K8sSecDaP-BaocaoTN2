#!/usr/bin/env bash
# Orchestrator demo phát hiện port-scan: sinh stream → chạy 3 thuật toán →
# in alerts + bảng so sánh. Ghi tất cả output vào output/portscan/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
OUT="$ROOT/output/portscan"
mkdir -p "$OUT"

if [[ ! -x "$BUILD/tools/portscan-pipeline/portscan-pipeline" ]]; then
    echo ">>> Build portscan-pipeline trước…"
    cmake -S "$ROOT" -B "$BUILD" -DBUILD_TOOLS=ON >/dev/null
    cmake --build "$BUILD" --target portscan-pipeline -j"$(nproc)" >/dev/null
fi

STREAM="$OUT/sample_stream.jsonl"
echo ">>> 1. Sinh 200 sự kiện (seed=42, attacker chèn từ event 100, 50 probes)…"
python3 "$ROOT/scripts/generate_portscan_stream.py" \
    --count 200 --attack-start 100 --scan-probes 50 --seed 42 \
    > "$STREAM"

echo ">>> 2. Chạy 3 thuật toán song song (CMS / MG / HashMap)…"
"$BUILD/tools/portscan-pipeline/portscan-pipeline" \
    --algo all \
    --input "$STREAM" \
    --trace-dir "$OUT" \
    --alerts "$OUT/alerts.json" \
    --comparison "$OUT/comparison.csv" \
    --threshold 40 --window-seconds 60 \
    --cms-width 2048 --cms-depth 5 --mg-k 64

echo ""
echo ">>> 3. Tổng hợp:"
echo "  Stream      : $STREAM"
echo "  Alerts JSON : $OUT/alerts.json"
echo "  Comparison  : $OUT/comparison.csv"
echo "  Trace files : $OUT/trace_{cms,mg,hashmap}.txt"
echo ""
echo "Bảng so sánh:"
column -s, -t < "$OUT/comparison.csv"
