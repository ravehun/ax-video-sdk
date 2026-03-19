#!/usr/bin/env bash
set -euo pipefail

BOARD_USER="${BOARD_USER:-root}"
BOARD_IP="${BOARD_IP:-10.126.35.203}"
BOARD_PASS="${BOARD_PASS:-123456}"
REMOTE_DIR="${REMOTE_DIR:-/tmp/ax_video_sdk_smoke}"
BUILD_DIR="${BUILD_DIR:-build_ax650_smoke_tc}"
INPUT_FILE="${INPUT_FILE:-pedestrian_thailand_1920x1080_30fps_5Mbps_hevc.mp4}"
RTSP_PATH="${RTSP_PATH:-dual_mux_gdb}"
MP4_FILE="${MP4_FILE:-dual_mux_gdb.mp4}"
GDB_LOG="${GDB_LOG:-dual_mux_gdb.log}"
EXPECTED_PACKETS="${EXPECTED_PACKETS:-5000}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-20}"
OUTPUT_SPEC="${OUTPUT_SPEC:-rtsp://127.0.0.1:8554/${RTSP_PATH},${REMOTE_DIR}/${MP4_FILE}}"
PROBE_URI="${PROBE_URI:-rtsp://127.0.0.1:8554/${RTSP_PATH}}"

SSH=(sshpass -p "${BOARD_PASS}" ssh -o PreferredAuthentications=password -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}")
SCP=(sshpass -p "${BOARD_PASS}" scp -o PreferredAuthentications=password -o StrictHostKeyChecking=no)

"${SCP[@]}" \
    "${BUILD_DIR}/libax_video_sdk.so" \
    "${BUILD_DIR}/ax_pipeline_smoke" \
    "${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/"

"${SSH[@]}" "
    set -e
    cd '${REMOTE_DIR}'
    rm -f '${MP4_FILE}' '${GDB_LOG}'

    gdb --batch \
        -ex 'handle SIG32 nostop noprint pass' \
        -ex 'handle SIGPIPE nostop noprint pass' \
        -ex run \
        -ex bt \
        --args env LD_LIBRARY_PATH=. ./ax_pipeline_smoke \
            --input '${INPUT_FILE}' \
            --output '${OUTPUT_SPEC}' \
            --codec h264 \
            --expected-packets '${EXPECTED_PACKETS}' \
            --timeout '${TIMEOUT_SECONDS}' > '${GDB_LOG}' 2>&1 &
    gdb_pid=\$!

    for _ in \$(seq 1 50); do
        if ss -lntp | grep -q '127.0.0.1:8554'; then
            break
        fi
        sleep 0.2
    done

    echo SS
    ss -lntp | grep 8554 || true
    echo FFPROBE
    timeout 3 ffprobe -v error -rtsp_transport tcp \
        -select_streams v:0 \
        -show_entries stream=codec_name,width,height,avg_frame_rate \
        -of default=noprint_wrappers=1 \
        '${PROBE_URI}' || echo ffprobe_failed

    set +e
    wait \$gdb_pid
    gdb_rc=\$?
    set -e
    echo GDB_RC:\${gdb_rc}
    echo FILE
    ls -lh '${MP4_FILE}' 2>/dev/null || echo missing
    echo GDB_LOG
    cat '${GDB_LOG}' 2>/dev/null || echo missing
"
