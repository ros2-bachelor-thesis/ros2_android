#!/usr/bin/env bash
# Convert YOLOv9 DualDDetect model (.pt) to NCNN format for Android ARM inference.
#
# Pipeline:
#   weights.pt
#     -> export_yolov9_patched.py (patches CBFuse + DualDDetect, traces to TorchScript)
#     -> pnnx inputshape=[1,3,352,640]f32 fp16=1  (PNNX emits FP16 NCNN bin directly)
#     -> sed (add Input layer shape annotation)
#     -> yolov9_s_pobed.ncnn.{param,bin}
#
# Note: ncnnoptimize is NOT used. PNNX with fp16=1 emits FP16 weights directly.
# ncnnoptimize segfaults on this model due to a shape-inference bug in ncnn 20260113.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: $(basename "$0") --yolov9-repo <path> [--model <path>] [--output-dir <path>] [--imgsz <H> <W>]

  --yolov9-repo <path>   Path to cloned Vermin_Collector_ROS2_3D_Object_Detection repo.
  --model <path>         Path to .pt weights file  (default: models/yolov9_s_pobed.pt)
  --output-dir <path>    Directory for output files  (default: output/)
  --imgsz <H> <W>        Input image size height width  (default: 352 640)
  -h, --help             Show this help message
EOF
}

# Defaults
MODEL_PATH="$SCRIPT_DIR/models/yolov9_s_pobed.pt"
OUTPUT_DIR="$SCRIPT_DIR/output"
YOLOV9_REPO=""
IMGSZ_H=352
IMGSZ_W=640

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --yolov9-repo)
            YOLOV9_REPO="$2"; shift 2 ;;
        --model)
            MODEL_PATH="$2"; shift 2 ;;
        --output-dir)
            OUTPUT_DIR="$2"; shift 2 ;;
        --imgsz)
            IMGSZ_H="$2"; IMGSZ_W="$3"; shift 3 ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

# Validate required argument
if [[ -z "$YOLOV9_REPO" ]]; then
    echo "Error: --yolov9-repo is required." >&2
    usage
    exit 1
fi

PATCHED_EXPORT="$SCRIPT_DIR/export_yolov9_patched.py"

# Check required tools
for tool in python3 pnnx; do
    if ! command -v "$tool" &>/dev/null; then
        echo "Error: '$tool' not found on PATH. See README.md for installation instructions." >&2
        exit 1
    fi
done

# Validate inputs
if [[ ! -f "$MODEL_PATH" ]]; then
    echo "Error: Model file not found: $MODEL_PATH" >&2
    exit 1
fi

if [[ ! -f "$PATCHED_EXPORT" ]]; then
    echo "Error: export_yolov9_patched.py not found at: $PATCHED_EXPORT" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

MODEL_STEM="$(basename "$MODEL_PATH" .pt)"

# Create temp working directory, cleaned up on exit
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "==> Working directory: $WORKDIR"
echo "==> Model: $MODEL_PATH"
echo "==> Input size: ${IMGSZ_H}x${IMGSZ_W}"
echo "==> Output: $OUTPUT_DIR"
echo ""

cp "$MODEL_PATH" "$WORKDIR/weights.pt"

cd "$WORKDIR"

echo "==> Step 1/3: Exporting to TorchScript (patching CBFuse + DualDDetect)..."
python3 "$PATCHED_EXPORT" \
    --weights weights.pt \
    --yolov9-repo "$YOLOV9_REPO" \
    --imgsz "$IMGSZ_H" "$IMGSZ_W"

if [[ ! -f weights.torchscript ]]; then
    echo "Error: weights.torchscript was not produced" >&2
    exit 1
fi

echo ""
echo "==> Step 2/3: Converting TorchScript to NCNN via PNNX (fp16=1)..."
pnnx weights.torchscript "inputshape=[1,3,${IMGSZ_H},${IMGSZ_W}]f32" fp16=1

if [[ ! -f weights.ncnn.param ]]; then
    echo "Error: weights.ncnn.param was not produced by pnnx" >&2
    exit 1
fi

echo ""
echo "==> Step 3/3: Adding Input layer shape annotation..."
sed -i "s/^\\(Input\\s*in0\\s*0 1 in0\\)\$/\\1 0=${IMGSZ_W} 1=${IMGSZ_H} 2=3/" weights.ncnn.param

cp weights.ncnn.param "$OUTPUT_DIR/${MODEL_STEM}.ncnn.param"
cp weights.ncnn.bin   "$OUTPUT_DIR/${MODEL_STEM}.ncnn.bin"

echo ""
echo "Done. Output files:"
echo "  $OUTPUT_DIR/${MODEL_STEM}.ncnn.param"
echo "  $OUTPUT_DIR/${MODEL_STEM}.ncnn.bin"
