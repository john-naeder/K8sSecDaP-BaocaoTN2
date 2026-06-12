#!/bin/sh

(cd report/stage1 && lualatex -interaction=nonstopmode -file-line-error main.tex && biber main || true && lualatex -interaction=nonstopmode -file-line-error main.tex && lualatex -interaction=nonstopmode -file-line-error main.tex) \
# && (cd report/stage2 && lualatex -interaction=nonstopmode -file-line-error main.tex && biber main || true && lualatex -interaction=nonstopmode -file-line-error main.tex && lualatex -interaction=nonstopmode -file-line-error main.tex)