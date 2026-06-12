# K8sSecDaP umbrella — convenience targets
.PHONY: help build report bc1 bc2 clean submodules

help:
	@echo "K8sSecDaP umbrella"
	@echo "  make submodules  - init/update all submodules"
	@echo "  make build       - build C++ (pipeline + tools) into build/bin"
	@echo "  make bc1 / bc2   - build thesis stage1 / stage2 PDF"
	@echo "  make clean       - remove build/"

submodules:
	git submodule update --init --recursive

build:
	cmake -S . -B build && cmake --build build -j

bc1:
	cd report/stage1 && latexmk -pdf main.tex

bc2:
	cd report/stage2 && latexmk -pdf main.tex

report: bc1 bc2

clean:
	rm -rf build
