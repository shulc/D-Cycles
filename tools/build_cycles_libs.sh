#!/usr/bin/env bash
# =====================================================================
# build_cycles_libs.sh — configure & build Cycles' static libraries from
# the Blender source tree (extern/blender/).
#
# Why a separate script (and not just add_subdirectory(extern/blender))?
# Blender's top-level CMakeLists.txt does its own `project(Blender)` and
# heavily relies on CMAKE_SOURCE_DIR pointing at the Blender repo root
# (for build_files/cmake/Modules etc). It does not compose cleanly when
# included as a subdirectory of another project. So we run it once
# directly to produce libcycles_*.a, then link our shim against them
# from D-Cycles' own CMakeLists.txt.
#
# Output:
#   extern/blender/build_cycles/lib/libcycles_*.a
#   extern/blender/build_cycles/lib/libbf_intern_*.a
#
# GPU support (opt-in via env vars):
#   OPTIX_ROOT_DIR=/path/to/NVIDIA-OptiX-SDK-X.x.x-linux64-x86_64
#     Enables WITH_CYCLES_DEVICE_OPTIX. Requires CUDA Toolkit (nvcc)
#     installed (sudo dnf install cuda-toolkit on Fedora).
#   WITH_CUDA=1
#     Enables WITH_CYCLES_DEVICE_CUDA in addition. Mostly redundant
#     with OptiX on RTX cards; useful as a fallback for non-RT GPUs.
#
# Usage:
#   tools/build_cycles_libs.sh                  # CPU only (default)
#   OPTIX_ROOT_DIR=~/optix tools/build_cycles_libs.sh   # CPU + OptiX
#   tools/build_cycles_libs.sh --reconfigure    # force re-run of CMake
# =====================================================================

set -euo pipefail

# Bring CUDA into PATH for nvcc discovery (Fedora 43 + cuda-toolkit RPM
# installs into /usr/local/cuda/bin which isn't in $PATH by default).
if [[ -d /usr/local/cuda/bin ]] && [[ ":$PATH:" != *":/usr/local/cuda/bin:"* ]]; then
    export PATH="/usr/local/cuda/bin:$PATH"
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BLENDER_SRC="${ROOT}/extern/blender"
BUILD_DIR="${BLENDER_SRC}/build_cycles"
GENERATOR=${CMAKE_GENERATOR:-Ninja}

# Detect platform for job count
if [[ "$OSTYPE" == "darwin"* ]]; then
    JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
else
    JOBS=${JOBS:-$(nproc)}
fi

if [[ ! -d "${BLENDER_SRC}/intern/cycles" ]]; then
    echo "Error: ${BLENDER_SRC}/intern/cycles missing. Run:" >&2
    echo "  git submodule update --init extern/blender" >&2
    exit 1
fi

# Detect platform-specific lib directory
if [[ "$OSTYPE" == "darwin"* ]]; then
    LIBDIR="${BLENDER_SRC}/lib/macos_arm64"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    LIBDIR="${BLENDER_SRC}/lib/windows_x64"
else
    LIBDIR="${BLENDER_SRC}/lib/linux_x64"
fi

if [[ ! -d "${LIBDIR}" ]]; then
    echo "Error: precompiled deps missing at ${LIBDIR}." >&2
    echo "Run:" >&2
    echo "  cd ${BLENDER_SRC}" >&2
    echo "  git config --local submodule.$(basename $LIBDIR).update checkout" >&2
    echo "  git submodule update --init --depth 1 $(basename $LIBDIR)" >&2
    exit 1
fi

if [[ "${1:-}" == "--reconfigure" ]] || [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo ">>> Configuring Cycles build at ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"

    # GPU toggles. OptiX requires CUDA Toolkit too (nvcc compiles the
    # device kernels). WITH_CUDA_DYNLOAD lets the runtime dlopen
    # libcuda so we don't link against /usr/local/cuda/lib64 directly.
    #
    # CUDA_HOST_COMPILER: nvcc needs a host C++ compiler whose <cmath>
    # / glibc headers it can parse. CUDA 13.1 chokes on gcc-15's glibc
    # noexcept on rsqrt; explicitly point at gcc-14 if available.
    GPU_ARGS=()
    NVCC_HOST=""
    if [[ -x /usr/bin/gcc-14 ]]; then
        NVCC_HOST=/usr/bin/gcc-14
    fi

    if [[ -n "${OPTIX_ROOT_DIR:-}" ]]; then
        if [[ ! -f "${OPTIX_ROOT_DIR}/include/optix.h" ]]; then
            echo "Error: OPTIX_ROOT_DIR=${OPTIX_ROOT_DIR} doesn't contain include/optix.h" >&2
            exit 1
        fi
        if ! command -v nvcc &>/dev/null; then
            echo "Error: nvcc not in PATH. OptiX needs CUDA Toolkit (sudo dnf install cuda-toolkit)." >&2
            exit 1
        fi
        echo ">>> GPU enabled: OptiX from ${OPTIX_ROOT_DIR}, nvcc from $(command -v nvcc)"
        [[ -n "${NVCC_HOST}" ]] && echo ">>> nvcc host compiler: ${NVCC_HOST}"
        GPU_ARGS=(
            -DWITH_CYCLES_DEVICE_OPTIX=ON
            -DOPTIX_ROOT_DIR="${OPTIX_ROOT_DIR}"
            -DWITH_CYCLES_DEVICE_CUDA=ON           # OptiX shares CUDA kernel infra
            -DWITH_CYCLES_CUDA_BINARIES=ON
            -DWITH_CUDA_DYNLOAD=ON
            # glibc 2.42 declares rsqrt under __USE_GNU with noexcept;
            # CUDA 13.1's crt/math_functions.h declares it without —
            # the redeclaration crashes nvcc. Undef _GNU_SOURCE on the
            # host preprocessor for kernel TUs to skip glibc's rsqrt.
            "-DCUDA_NVCC_FLAGS=-Xcompiler;-U_GNU_SOURCE"
            # CUDA 13.1's IR validator chokes on some Cycles SVM
            # kernels (svm_node_tex_voxel) for sm_86/89. Build only
            # sm_75 + compute_75 PTX — the CUDA driver JIT-compiles
            # PTX 7.5 to any sm >= 7.5 at runtime, which covers all
            # RTX cards (sm_86/89/120 etc).
            "-DCYCLES_CUDA_BINARIES_ARCH=sm_75;compute_75"
        )
        [[ -n "${NVCC_HOST}" ]] && GPU_ARGS+=(-DCUDA_HOST_COMPILER="${NVCC_HOST}")
    elif [[ "${WITH_CUDA:-0}" == "1" ]]; then
        if ! command -v nvcc &>/dev/null; then
            echo "Error: nvcc not in PATH. CUDA needs the toolkit installed." >&2
            exit 1
        fi
        echo ">>> GPU enabled: CUDA only (no OptiX), nvcc from $(command -v nvcc)"
        [[ -n "${NVCC_HOST}" ]] && echo ">>> nvcc host compiler: ${NVCC_HOST}"
        GPU_ARGS=(
            -DWITH_CYCLES_DEVICE_CUDA=ON
            -DWITH_CYCLES_DEVICE_OPTIX=OFF
            -DWITH_CYCLES_CUDA_BINARIES=ON
            -DWITH_CUDA_DYNLOAD=ON
            "-DCUDA_NVCC_FLAGS=-Xcompiler;-U_GNU_SOURCE"
            "-DCYCLES_CUDA_BINARIES_ARCH=sm_75;compute_75"
        )
        [[ -n "${NVCC_HOST}" ]] && GPU_ARGS+=(-DCUDA_HOST_COMPILER="${NVCC_HOST}")
    else
        echo ">>> CPU only (set OPTIX_ROOT_DIR or WITH_CUDA=1 to enable GPU)"
        GPU_ARGS=(
            -DWITH_CYCLES_DEVICE_CUDA=OFF
            -DWITH_CYCLES_DEVICE_OPTIX=OFF
            -DWITH_CYCLES_CUDA_BINARIES=OFF
        )
    fi

    # Metal: opt-in on macOS via WITH_METAL=1 (mirrors WITH_CUDA on
    # Linux/Windows). Default off because Cycles' metal kernel build
    # has fragile path expectations — easier to ship a working CPU
    # baseline first and let users flip the toggle when their setup
    # is ready. CPU rendering on macOS works regardless of this.
    if [[ "$OSTYPE" == "darwin"* ]] && [[ "${WITH_METAL:-0}" == "1" ]]; then
        echo ">>> Metal device enabled (macOS, opt-in via WITH_METAL=1)"
        METAL_ARG="-DWITH_CYCLES_DEVICE_METAL=ON"
    else
        METAL_ARG="-DWITH_CYCLES_DEVICE_METAL=OFF"
    fi

    cmake -S "${BLENDER_SRC}" -B "${BUILD_DIR}" -G "${GENERATOR}" \
        -DCMAKE_BUILD_TYPE=Release \
        \
        -DWITH_BLENDER=OFF \
        -DWITH_CYCLES=ON \
        -DWITH_CYCLES_STANDALONE=ON \
        -DWITH_CYCLES_STANDALONE_GUI=OFF \
        -DWITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF \
        -DWITH_CYCLES_TEST=OFF \
        \
        "${GPU_ARGS[@]}" \
        "${METAL_ARG}" \
        -DWITH_CYCLES_DEVICE_HIP=OFF \
        -DWITH_CYCLES_DEVICE_ONEAPI=OFF \
        -DWITH_CYCLES_HIP_BINARIES=OFF \
        \
        -DWITH_CYCLES_EMBREE="${WITH_EMBREE:-ON}" \
        -DWITH_CYCLES_OSL=OFF \
        -DWITH_CYCLES_OPENSUBDIV=OFF \
        -DWITH_CYCLES_PATH_GUIDING=OFF \
        -DWITH_OPENIMAGEDENOISE=ON \
        -DWITH_OPENCOLORIO=ON \
        -DWITH_OPENVDB=OFF \
        -DWITH_NANOVDB=OFF \
        -DWITH_ALEMBIC=OFF \
        -DWITH_USD=OFF \
        -DWITH_MATERIALX=OFF \
        -DWITH_HYDRA=OFF \
        \
        -DWITH_PYTHON=ON \
        -DWITH_PYTHON_MODULE=OFF \
        -DWITH_AUDASPACE=OFF \
        -DWITH_OPENAL=OFF \
        -DWITH_CODEC_FFMPEG=OFF \
        -DWITH_CODEC_SNDFILE=OFF \
        -DWITH_SDL=OFF \
        -DWITH_INTERNATIONAL=OFF \
        -DWITH_INPUT_NDOF=OFF \
        -DWITH_XR_OPENXR=OFF \
        -DWITH_DRACO=OFF \
        -DWITH_LIBMV=OFF \
        -DWITH_OPENCOLLADA=OFF \
        -DWITH_FFTW3=OFF \
        -DWITH_BULLET=OFF \
        -DWITH_GMP=OFF \
        -DWITH_MANIFOLD=OFF \
        -DWITH_HARU=OFF \
        -DWITH_MOD_FLUID=OFF \
        -DWITH_MOD_OCEANSIM=OFF \
        -DWITH_HARFBUZZ=OFF \
        -DWITH_FRIBIDI=OFF \
        -DWITH_POTRACE=OFF \
        -DWITH_QUADRIFLOW=OFF \
        -DWITH_UV_SLIM=OFF \
        \
        -DWITH_IMAGE_OPENEXR=ON \
        -DWITH_IMAGE_OPENJPEG=ON \
        -DWITH_IMAGE_WEBP=ON \
        -DWITH_IMAGE_CINEON=OFF \
        \
        -DWITH_IO_WAVEFRONT_OBJ=OFF \
        -DWITH_IO_PLY=OFF \
        -DWITH_IO_STL=OFF \
        -DWITH_IO_FBX=OFF \
        -DWITH_IO_GREASE_PENCIL=OFF \
        \
        -DWITH_GHOST_SDL=OFF
fi

echo ">>> Building cycles target (-j ${JOBS})"
# NVCC_APPEND_FLAGS is respected by every nvcc invocation (CUDA kernels,
# OptiX PTX kernels) — the per-macro CUDA_NVCC_FLAGS in Cycles' CMake
# doesn't reach the OptiX path otherwise. -U_GNU_SOURCE hides glibc
# 2.42's noexcept rsqrt declaration so CUDA 13.1's headers don't clash.
NVCC_APPEND_FLAGS="-Xcompiler -U_GNU_SOURCE" \
    cmake --build "${BUILD_DIR}" --target cycles -j "${JOBS}"

# Stage kernel binaries into ${BUILD_DIR}/bin/lib so the Cycles runtime
# can find them via path_get("lib/kernel_*.ptx.zst"). cmake --install
# does this for us; we skip its RPATH-rewriting side effect on the
# (unused-by-us) standalone executable by only installing the kernels
# we care about via direct copy.
KERNEL_DIR="${BUILD_DIR}/intern/cycles/kernel"
DEST_DIR="${BUILD_DIR}/bin/lib"
if compgen -G "${KERNEL_DIR}/*.zst" >/dev/null; then
    mkdir -p "${DEST_DIR}"
    cp -u "${KERNEL_DIR}"/*.zst "${DEST_DIR}/" 2>/dev/null || true
fi

# Stage Cycles' source tree under ${BUILD_DIR}/bin/source so the Metal
# device can find kernel.metal via path_get("source/kernel/device/metal/
# kernel.metal"). delayed_install populates this only when cmake --install
# runs, which we avoid (it rewrites RPATH on the unused standalone binary
# and pollutes the dev tree). A symlink achieves the same path resolution
# at zero cost. Always recreate so an old cmake-install copy or a stale
# symlink target gets refreshed.
mkdir -p "${BUILD_DIR}/bin"
rm -rf "${BUILD_DIR}/bin/source"
ln -sfn "${BLENDER_SRC}/intern/cycles" "${BUILD_DIR}/bin/source"
echo ">>> Staged Cycles source tree: ${BUILD_DIR}/bin/source -> ${BLENDER_SRC}/intern/cycles"

echo
echo ">>> Done."
echo "  Static libs:  ${BUILD_DIR}/lib/"
echo "  GPU binaries: ${DEST_DIR}/"
echo "  Runtime root: ${BUILD_DIR}/bin (baked into shim as default; override with CYCLESC_KERNEL_PATH)"
