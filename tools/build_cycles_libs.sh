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
#   extern/blender/build_cycles/intern/cycles/{session,scene,kernel,
#     device,bvh,subd,graph,util}/libcycles_*.a
#   extern/blender/build_cycles/intern/{libc_compat,guardedalloc,sky}/
#     libbf_intern_*.a
#
# Usage:
#   tools/build_cycles_libs.sh           # configure if needed, then build
#   tools/build_cycles_libs.sh --reconfigure   # force re-run of CMake
# =====================================================================

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BLENDER_SRC="${ROOT}/extern/blender"
BUILD_DIR="${BLENDER_SRC}/build_cycles"
GENERATOR=${CMAKE_GENERATOR:-Ninja}
JOBS=${JOBS:-$(nproc)}

if [[ ! -d "${BLENDER_SRC}/intern/cycles" ]]; then
    echo "Error: ${BLENDER_SRC}/intern/cycles missing. Run:" >&2
    echo "  git submodule update --init extern/blender" >&2
    exit 1
fi
if [[ ! -d "${BLENDER_SRC}/lib/linux_x64" ]]; then
    echo "Error: precompiled deps missing at ${BLENDER_SRC}/lib/linux_x64." >&2
    echo "Run:" >&2
    echo "  cd ${BLENDER_SRC}" >&2
    echo "  git config --local submodule.lib/linux_x64.update checkout" >&2
    echo "  git submodule update --init --depth 1 lib/linux_x64" >&2
    exit 1
fi

if [[ "${1:-}" == "--reconfigure" ]] || [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo ">>> Configuring Cycles build at ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
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
        -DWITH_CYCLES_DEVICE_CUDA=OFF \
        -DWITH_CYCLES_DEVICE_OPTIX=OFF \
        -DWITH_CYCLES_DEVICE_HIP=OFF \
        -DWITH_CYCLES_DEVICE_METAL=OFF \
        -DWITH_CYCLES_DEVICE_ONEAPI=OFF \
        -DWITH_CYCLES_CUDA_BINARIES=OFF \
        \
        -DWITH_CYCLES_EMBREE=ON \
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
cmake --build "${BUILD_DIR}" --target cycles -j "${JOBS}"

echo
echo ">>> Done. Artifacts:"
find "${BUILD_DIR}/intern/cycles" -name 'libcycles_*.a' 2>/dev/null \
    | sort | head -20
