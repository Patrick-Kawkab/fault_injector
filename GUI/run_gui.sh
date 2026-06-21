#!/usr/bin/env bash
cd "$(dirname "$0")" || exit 1
# To use the real C++ injector instead of the mock, uncomment + adjust these:
# export FI_INJECTOR="$HOME/Desktop/fault_injector/mainn"
# export FI_INJECTOR_CWD="$HOME/Desktop/fault_injector"
exec python3 main.py
