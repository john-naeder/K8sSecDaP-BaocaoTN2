# K8sSecDaP-report

Báo cáo đồ án tốt nghiệp (LaTeX) cho **K8sSecDaP** — Port-scan K8s Detect-and-Prevent.

## Layout
- `stage1/` — báo cáo giai đoạn 1.
- `stage2/` — báo cáo giai đoạn 2.
- `shared/` — chapters & figures dùng chung.

## Build
```bash
./report-compile.sh           # hoặc:
cd stage1 && latexmk -pdf main.tex
```
PDF/aux là artifact generated (xem `.gitignore`).
