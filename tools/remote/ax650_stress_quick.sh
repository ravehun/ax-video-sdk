#!/usr/bin/env bash
set -euo pipefail

BOARD_USER="${BOARD_USER:-root}"
BOARD_IP="${BOARD_IP:-10.126.35.203}"
BOARD_PASS="${BOARD_PASS:-123456}"
REMOTE_DIR="${REMOTE_DIR:-/tmp/ax_video_sdk_smoke}"
BUILD_DIR="${BUILD_DIR:-build_ax650_smoke_tc}"
INPUT_FILE="${INPUT_FILE:-pedestrian_thailand_1920x1080_30fps_5Mbps_hevc.mp4}"
FRAME_COPY_MODE="${FRAME_COPY_MODE:-none}"
FRAME_COPY_INTERVAL="${FRAME_COPY_INTERVAL:-33}"
CHANNELS="${CHANNELS:-16}"
CODEC="${CODEC:-h264}"
LOOP_PLAYBACK="${LOOP_PLAYBACK:-0}"
EXPECTED_PACKETS="${EXPECTED_PACKETS:-180}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-30}"
RUN_PREFIX="${RUN_PREFIX:-quick_${FRAME_COPY_MODE}}"
LOOP_FLAG=""
if [[ "${LOOP_PLAYBACK}" == "1" ]]; then
    LOOP_FLAG="--loop"
fi

SSH=(sshpass -p "${BOARD_PASS}" ssh -o PreferredAuthentications=password -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}")
SCP=(sshpass -p "${BOARD_PASS}" scp -o PreferredAuthentications=password -o StrictHostKeyChecking=no)

"${SCP[@]}" \
    "${BUILD_DIR}/libax_video_sdk.so" \
    "${BUILD_DIR}/ax_pipeline_stress_smoke" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/"

"${SSH[@]}" "
    set -e
    cd '${REMOTE_DIR}'
    rm -f '${RUN_PREFIX}.log'
    set +e
    LD_LIBRARY_PATH=. ./ax_pipeline_stress_smoke \
        --input '${INPUT_FILE}' \
        --channels '${CHANNELS}' \
        --codec '${CODEC}' \
        ${LOOP_FLAG} \
        --frame-copy-mode '${FRAME_COPY_MODE}' \
        --frame-copy-interval '${FRAME_COPY_INTERVAL}' \
        --expected-packets '${EXPECTED_PACKETS}' \
        --timeout '${TIMEOUT_SECONDS}' > '${RUN_PREFIX}.log' 2>&1
    run_rc=\$?
    set -e
    echo RUN_RC:\${run_rc}
    echo LOGTAIL
    tail -n 120 '${RUN_PREFIX}.log'
"
