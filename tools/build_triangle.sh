#!/usr/bin/env bash
# =====================================================================
# build_triangle.sh — compile examples/triangle directly via ldc2.
#
# Why not dub? dub's lflags handling makes it painful to pass the
# 30+ library / rpath flags our cyclesc static lib needs. Calling ldc2
# directly is straightforward and keeps the integration testable.
#
# Pre-req: tools/build_cycles_libs.sh and then `cmake --build build`
# with WITH_CYCLES=ON.
# =====================================================================

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BLENDER_SRC="${ROOT}/extern/blender"
LIBDIR="${BLENDER_SRC}/lib/linux_x64"
CY_BUILD="${BLENDER_SRC}/build_cycles"
OUR_BUILD="${ROOT}/build"

if [[ ! -f "${OUR_BUILD}/libcyclesc.a" ]]; then
    echo "Error: ${OUR_BUILD}/libcyclesc.a missing. Run:" >&2
    echo "  cmake -S ${ROOT} -B ${OUR_BUILD} -G Ninja -DWITH_CYCLES=ON" >&2
    echo "  cmake --build ${OUR_BUILD} --target cyclesc" >&2
    exit 1
fi

OUT="${ROOT}/examples/triangle/triangle"

ldc2 \
    -of="${OUT}" \
    -I="${ROOT}/source" \
    "${ROOT}/source/cycles/c.d" \
    "${ROOT}/examples/triangle/source/app.d" \
    \
    -L=-Wl,--start-group \
    "-L=${OUR_BUILD}/libcyclesc.a" \
    "-L=${CY_BUILD}/lib/libcycles_session.a" \
    "-L=${CY_BUILD}/lib/libcycles_scene.a" \
    "-L=${CY_BUILD}/lib/libcycles_kernel.a" \
    "-L=${CY_BUILD}/lib/libcycles_device.a" \
    "-L=${CY_BUILD}/lib/libcycles_integrator.a" \
    "-L=${CY_BUILD}/lib/libcycles_bvh.a" \
    "-L=${CY_BUILD}/lib/libcycles_subd.a" \
    "-L=${CY_BUILD}/lib/libcycles_graph.a" \
    "-L=${CY_BUILD}/lib/libcycles_util.a" \
    "-L=${CY_BUILD}/lib/libbf_intern_libc_compat.a" \
    "-L=${CY_BUILD}/lib/libbf_intern_guardedalloc.a" \
    "-L=${CY_BUILD}/lib/libbf_intern_sky.a" \
    "-L=${CY_BUILD}/lib/libextern_glog.a" \
    "-L=${CY_BUILD}/lib/libextern_gflags.a" \
    -L=-Wl,--end-group \
    \
    "-L=${LIBDIR}/openimageio/lib/libOpenImageIO.so" \
    "-L=${LIBDIR}/openimageio/lib/libOpenImageIO_Util.so" \
    "-L=${LIBDIR}/opencolorio/lib/libOpenColorIO.so" \
    "-L=${LIBDIR}/embree/lib/libembree4.so" \
    "-L=${LIBDIR}/openimagedenoise/lib/libOpenImageDenoise.so" \
    "-L=${LIBDIR}/openexr/lib/libOpenEXR.so" \
    "-L=${LIBDIR}/openexr/lib/libOpenEXRCore.so" \
    "-L=${LIBDIR}/openexr/lib/libIex.so" \
    "-L=${LIBDIR}/openexr/lib/libIlmThread.so" \
    "-L=${LIBDIR}/imath/lib/libImath.so" \
    "-L=${LIBDIR}/opensubdiv/lib/libosdCPU.so" \
    "-L=${LIBDIR}/dpcpp/lib/libsycl.so" \
    "-L=${LIBDIR}/tbb/lib/libtbb.so" \
    \
    "-L=${LIBDIR}/pugixml/lib/libpugixml.a" \
    "-L=${LIBDIR}/png/lib/libpng16.a" \
    "-L=${LIBDIR}/jpeg/lib/libjpeg.a" \
    "-L=${LIBDIR}/tiff/lib/libtiff.a" \
    "-L=${LIBDIR}/openjpeg/lib/libopenjp2.a" \
    "-L=${LIBDIR}/webp/lib/libwebp.a" \
    "-L=${LIBDIR}/zlib/lib/libz.a" \
    "-L=${LIBDIR}/zstd/lib/libzstd.a" \
    \
    "-L=-Wl,-rpath,${LIBDIR}/openimageio/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/opencolorio/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/embree/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/openimagedenoise/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/openexr/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/imath/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/opensubdiv/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/dpcpp/lib" \
    "-L=-Wl,-rpath,${LIBDIR}/tbb/lib" \
    \
    -L=-lstdc++ -L=-lpthread -L=-lm -L=-ldl

echo "Built ${OUT}"
