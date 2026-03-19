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
LOOP_PLAYBACK="${LOOP_PLAYBACK:-1}"
DEFAULT_EXPECTED_PACKETS="240"
if [[ "${LOOP_PLAYBACK}" == "1" ]]; then
    DEFAULT_EXPECTED_PACKETS="1800"
fi
EXPECTED_PACKETS="${EXPECTED_PACKETS:-${DEFAULT_EXPECTED_PACKETS}}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-60}"
RUN_PREFIX="${RUN_PREFIX:-stress_${FRAME_COPY_MODE}}"
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

    echo PRE_FREE
    free -m
    echo PRE_CMM
    cat /proc/ax_proc/mem_cmm_info | head -n 6

    rm -f '${RUN_PREFIX}.log'
    LD_LIBRARY_PATH=. ./ax_pipeline_stress_smoke \
        --input '${INPUT_FILE}' \
        --channels '${CHANNELS}' \
        --codec '${CODEC}' \
        ${LOOP_FLAG} \
        --frame-copy-mode '${FRAME_COPY_MODE}' \
        --frame-copy-interval '${FRAME_COPY_INTERVAL}' \
        --expected-packets '${EXPECTED_PACKETS}' \
        --timeout '${TIMEOUT_SECONDS}' > '${RUN_PREFIX}.log' 2>&1 &
    pid=\$!

    sleep 15
    echo SAMPLE1_PS
    ps -p \$pid -o pid,%cpu,%mem,rss,vsz,comm || true
    echo SAMPLE1_TOP
    top -b -n 1 | head -n 12
    echo SAMPLE1_FREE
    free -m
    echo SAMPLE1_CMM
    cat /proc/ax_proc/mem_cmm_info | head -n 6

    sleep 30
    echo SAMPLE2_PS
    ps -p \$pid -o pid,%cpu,%mem,rss,vsz,comm || true
    echo SAMPLE2_TOP
    top -b -n 1 | head -n 12
    echo SAMPLE2_FREE
    free -m
    echo SAMPLE2_CMM
    cat /proc/ax_proc/mem_cmm_info | head -n 6

    set +e
    wait \$pid
    wait_rc=\$?
    set -e
    echo WAIT_RC:\${wait_rc}
    echo LOGTAIL
    tail -n 120 '${RUN_PREFIX}.log'
"
