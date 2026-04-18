#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2"
BUILD_DIR="${ROOT_DIR}/build"
PATTERN_PNG="${ROOT_DIR}/Artifacts/pattern.png"

# Allow override if OpenCV is installed elsewhere.
OPENCV_DIR_DEFAULT="/opt/opencv-3.4-opengl/share/OpenCV"
OPENCV_DIR="${OPENCV_DIR:-${OPENCV_DIR_DEFAULT}}"

cd "${ROOT_DIR}"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

cd "${BUILD_DIR}"
cmake .. -DOpenCV_DIR="${OPENCV_DIR}"
make -j"$(nproc)"

echo "[CHECK] Linked OpenCV libs:"
if ! ldd ./src/ARProject.out | grep -i opencv; then
  echo "[WARN] No OpenCV entries found in ldd output."
fi

cd src
exec ./ARProject.out "${PATTERN_PNG}"
