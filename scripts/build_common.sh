#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <chip>" >&2
    exit 1
fi

CHIP="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "${CHIP}" in
    ax650)
        MSP_ZIP_NAME="msp_50_3.10.2.zip"
        MSP_ZIP_DEFAULT="${ROOT_DIR}/.ci/downloads/${MSP_ZIP_NAME}"
        MSP_EXTRACT_DIR="${ROOT_DIR}/.ci/msp/ax650"
        MSP_ROOT="${MSP_EXTRACT_DIR}/msp"
        TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/aarch64-none-linux-gnu.toolchain.cmake"
        DEFAULT_TOOLCHAIN_BIN="${ROOT_DIR}/.ci/toolchains/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/bin"
        COMPILER_CHECK="aarch64-none-linux-gnu-g++"
        ;;
    ax630c)
        MSP_ZIP_NAME="msp_20e_3.0.0.zip"
        MSP_ZIP_DEFAULT="${ROOT_DIR}/.ci/downloads/${MSP_ZIP_NAME}"
        MSP_EXTRACT_DIR="${ROOT_DIR}/.ci/msp/ax620e"
        MSP_ROOT="${MSP_EXTRACT_DIR}/msp"
        TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/aarch64-none-linux-gnu.toolchain.cmake"
        DEFAULT_TOOLCHAIN_BIN="${ROOT_DIR}/.ci/toolchains/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/bin"
        COMPILER_CHECK="aarch64-none-linux-gnu-g++"
        ;;
    ax620q)
        MSP_ZIP_NAME="msp_20e_3.0.0.zip"
        MSP_ZIP_DEFAULT="${ROOT_DIR}/.ci/downloads/${MSP_ZIP_NAME}"
        MSP_EXTRACT_DIR="${ROOT_DIR}/.ci/msp/ax620e"
        MSP_ROOT="${MSP_EXTRACT_DIR}/msp"
        TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/arm-AX620E-linux-uclibcgnueabihf.toolchain.cmake"
        DEFAULT_TOOLCHAIN_BIN="${ROOT_DIR}/.ci/toolchains/arm-AX620E-linux-uclibcgnueabihf/bin"
        COMPILER_CHECK="arm-AX620E-linux-uclibcgnueabihf-g++"
        ;;
    ax620qp)
        MSP_ZIP_NAME="msp_20e_3.0.0.zip"
        MSP_ZIP_DEFAULT="${ROOT_DIR}/.ci/downloads/${MSP_ZIP_NAME}"
        MSP_EXTRACT_DIR="${ROOT_DIR}/.ci/msp/ax620e"
        MSP_ROOT="${MSP_EXTRACT_DIR}/msp"
        TOOLCHAIN_FILE="${ROOT_DIR}/toolchains/arm-linux-gnueabihf.toolchain.cmake"
        DEFAULT_TOOLCHAIN_BIN="${ROOT_DIR}/.ci/toolchains/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin"
        COMPILER_CHECK="arm-linux-gnueabihf-g++"
        ;;
    *)
        echo "unsupported chip: ${CHIP}" >&2
        exit 1
        ;;
esac

MSP_ZIP_PATH="${AXSDK_MSP_ZIP_PATH:-${MSP_ZIP_DEFAULT}}"
TOOLCHAIN_BIN="${AXSDK_TOOLCHAIN_BIN:-${DEFAULT_TOOLCHAIN_BIN}}"
BUILD_DIR="${AXSDK_BUILD_DIR:-${ROOT_DIR}/build_${CHIP}_ci}"
STAGE_DIR="${AXSDK_STAGE_DIR:-${ROOT_DIR}/artifacts/${CHIP}}"
PACKAGE_BASENAME="ax_video_sdk_${CHIP}"
PACKAGE_DIR="${STAGE_DIR}/${PACKAGE_BASENAME}"

if [[ ! -f "${MSP_ZIP_PATH}" ]]; then
    echo "missing MSP zip: ${MSP_ZIP_PATH}" >&2
    exit 1
fi

if [[ -d "${TOOLCHAIN_BIN}" ]]; then
    export PATH="${TOOLCHAIN_BIN}:${PATH}"
fi

if ! command -v "${COMPILER_CHECK}" >/dev/null 2>&1; then
    echo "missing compiler ${COMPILER_CHECK}; expected under ${TOOLCHAIN_BIN} or available in PATH" >&2
    exit 1
fi

"${COMPILER_CHECK}" -v >/dev/null 2>&1 || {
    echo "compiler probe failed: ${COMPILER_CHECK} -v" >&2
    exit 1
}

mkdir -p "${MSP_EXTRACT_DIR}"
if [[ ! -d "${MSP_ROOT}" ]]; then
    rm -rf "${MSP_EXTRACT_DIR}"
    mkdir -p "${MSP_EXTRACT_DIR}"
    unzip -q "${MSP_ZIP_PATH}" -d "${MSP_EXTRACT_DIR}"
fi

if [[ ! -d "${MSP_ROOT}" ]]; then
    DETECTED_MSP_ROOT="$(find "${MSP_EXTRACT_DIR}" -maxdepth 3 -type d -name msp | head -n 1 || true)"
    if [[ -n "${DETECTED_MSP_ROOT}" ]]; then
        MSP_ROOT="${DETECTED_MSP_ROOT}"
    else
        echo "extracted MSP root not found: ${MSP_ROOT}" >&2
        exit 1
    fi
fi

rm -rf "${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DAXSDK_CHIP_TYPE="${CHIP}" \
    -DAXSDK_MSP_DIR="${MSP_ROOT}" \
    -DAXSDK_BUILD_SHARED=ON \
    -DAXSDK_BUILD_TOOLS=OFF \
    -DAXSDK_BUILD_SMOKE_TESTS=OFF

cmake --build "${BUILD_DIR}" -j"$(getconf _NPROCESSORS_ONLN)"

rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/lib"
cp -a "${ROOT_DIR}/include" "${PACKAGE_DIR}/include"
cp -a "${BUILD_DIR}/libax_video_sdk.so" "${PACKAGE_DIR}/lib/"

cat > "${PACKAGE_DIR}/BUILD_INFO.txt" <<EOF
chip=${CHIP}
build_dir=${BUILD_DIR}
msp_zip=${MSP_ZIP_PATH}
msp_root=${MSP_ROOT}
toolchain_file=${TOOLCHAIN_FILE}
toolchain_bin=${TOOLCHAIN_BIN}
compiler=$("${COMPILER_CHECK}" --version | head -n 1)
EOF

(
    cd "${STAGE_DIR}"
    tar -czf "${PACKAGE_BASENAME}.tar.gz" "${PACKAGE_BASENAME}"
)

echo "package=${STAGE_DIR}/${PACKAGE_BASENAME}.tar.gz"
