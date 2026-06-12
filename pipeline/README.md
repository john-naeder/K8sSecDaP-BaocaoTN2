# K8sSecDaP-pipeline

Detection pipeline engine (C++17) cho **K8sSecDaP** + thư viện DSA dùng chung.

## Layout
- `pipeline/` — engine `zt-pipeline` (CMS, Tarjan SCC, BFS reachability, LPM IP-class) đọc network
  events (file/stdin) → phát hiện port-scan/SCC/blast-radius → xuất alert.
- `libdsa/` — thư viện cấu trúc dữ liệu & giải thuật (target CMake `dsa`).

## Build
```bash
cmake -S . -B build && cmake --build build -j
./build/pipeline/zt-pipeline --help
```

> Container image (`Dockerfile.pipeline`) được build ở repo **K8sSecDaP** (umbrella) vì cần gộp
> thêm `tools/`, `scripts/`, `config/`, entrypoints từ `soc/` — là artifact tích hợp đa-component.
