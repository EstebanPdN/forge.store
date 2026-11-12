#!/bin/bash
# Abre el prototipo de forge. store en http://localhost:8642 (doble clic en macOS).
cd "$(dirname "$0")"
PORT=8642
(sleep 1 && open "http://localhost:$PORT") &
python3 tools/serve.py "$PORT"
