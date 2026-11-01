#!/usr/bin/env bash
# Sets up the CPU MADLAD translation sidecar in a local virtualenv and converts
# the model to CTranslate2 int8 if it is not present.
#
# Usage:
#   ./server/translation/setup_sidecar.sh
#   MADLAD_MODEL_ID=google/madlad400-3b-mt ./server/translation/setup_sidecar.sh
set -euo pipefail
cd "$(dirname "$0")"

model_id="${MADLAD_MODEL_ID:-google/madlad400-3b-mt}"
model_dir="${MADLAD_MODEL_DIR:-models/madlad400-3b-ct2-int8}"

python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
pip install "transformers>=4.40" "ctranslate2>=4.0"

if [[ ! -f "${model_dir}/model.bin" ]]; then
  mkdir -p "$(dirname "$model_dir")"
  echo "Converting ${model_id} -> ${model_dir} (this downloads several GB)..."
  ct2-transformers-converter --model "$model_id" --output_dir "$model_dir" \
    --quantization int8
fi

echo "Model ready at ${model_dir}"
echo "Start it with: ./server/translation/run_sidecar.sh"
