# =====================================================================
# D-Cycles — C shim over Cycles X (Blender's path tracer).
#
# Two build modes:
#   - WITH_CYCLES=OFF (default): stub library that returns CYC_ERR_UNSUPPORTED
#     from every entry point. Useful so the D bindings and downstream
#     vibe3d code link/compile during early development.
#   - WITH_CYCLES=ON: links real Cycles. Requires the Cycles static libs
#     to have been built first by:
#         tools/build_cycles_libs.sh
#     This script runs Blender's own CMake on extern/blender/ (with
#     WITH_BLENDER=OFF) to produce libcycles_*.a inside
#     extern/blender/build_cycles/. We then link our shim against those.
#
# Why not add_subdirectory(extern/blender)? Blender's top-level CMake
# calls project(Blender), depends on CMAKE_SOURCE_DIR pointing at its
# own root, and pulls 1000+ lines of platform detection that don't
# compose with a parent project. Building Cycles' libs in a sibling
# tree and linking the products keeps the boundary clean.
# =====================================================================

cmake_minimum_required(VERSION 3.20)
project(d_cycles LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(WITH_CYCLES "Build against real Cycles from extern/blender" OFF)

# ---------------------------------------------------------------------
# Source layout differs between stub and real backend.
# ---------------------------------------------------------------------
if(WITH_CYCLES)
    set(CYCLESC_SOURCES
        csrc/cyclesc_common.cpp
        csrc/cyclesc_devices.cpp
        csrc/cyclesc_session.cpp
        csrc/cyclesc_scene.cpp
        csrc/cyclesc_mesh.cpp
        csrc/cyclesc_object.cpp
        csrc/cyclesc_light.cpp
        csrc/cyclesc_camera.cpp
        csrc/cyclesc_shader.cpp
        csrc/cyclesc_render.cpp
    )
else()
    set(CYCLESC_SOURCES csrc/cyclesc.cpp)
endif()

add_library(cyclesc STATIC ${CYCLESC_SOURCES})
target_include_directories(cyclesc PUBLIC csrc)

# ---------------------------------------------------------------------
# Real Cycles wiring — Phase 0c.
# ---------------------------------------------------------------------
if(WITH_CYCLES)
    set(BLENDER_SRC "${CMAKE_SOURCE_DIR}/extern/blender"
        CACHE PATH "Path to Blender source tree (submodule)")
    set(CYCLES_BUILD_DIR "${BLENDER_SRC}/build_cycles"
        CACHE PATH "Where tools/build_cycles_libs.sh produced libcycles_*.a")
    set(LIBDIR "${BLENDER_SRC}/lib/linux_x64"
        CACHE PATH "Blender precompiled deps tree (lib/<platform>_<arch>)")

    if(NOT EXISTS "${BLENDER_SRC}/intern/cycles/CMakeLists.txt")
        message(FATAL_ERROR
            "Cycles source not found at ${BLENDER_SRC}/intern/cycles. "
            "Run: git submodule update --init extern/blender")
    endif()
    if(NOT EXISTS "${LIBDIR}/openimageio")
        message(FATAL_ERROR
            "Precompiled deps not found at ${LIBDIR}. Run:\n"
            "  git -C extern/blender config --local submodule.lib/linux_x64.update checkout\n"
            "  git -C extern/blender submodule update --init --depth 1 lib/linux_x64")
    endif()
    if(NOT EXISTS "${CYCLES_BUILD_DIR}/lib/libcycles_session.a")
        message(FATAL_ERROR
            "Cycles static libs not built. Run:\n"
            "  tools/build_cycles_libs.sh\n"
            "This produces lib*.a in ${CYCLES_BUILD_DIR}/lib/.")
    endif()

    # ---- Cycles core + helper libs (IMPORTED) ------------------------
    # All static libs land in ${CYCLES_BUILD_DIR}/lib/. Link order
    # matches cycles_standalone in extern/blender/intern/cycles/app/.
    # Circular deps between static libs need --start-group/--end-group
    # at link time (handled below).
    set(_cycles_static_libs
        cycles_session cycles_scene cycles_kernel cycles_device
        cycles_integrator cycles_bvh cycles_subd cycles_graph cycles_util
        bf_intern_libc_compat bf_intern_guardedalloc bf_intern_sky
        extern_glog extern_gflags
    )
    # WITH_CYCLES_DEVICE_CUDA/OPTIX builds drop extern_cuew (CUDA dynload
    # stubs) alongside. Detect it on disk so a CPU-only build doesn't
    # require the lib to exist.
    if(EXISTS "${CYCLES_BUILD_DIR}/lib/libextern_cuew.a")
        list(APPEND _cycles_static_libs extern_cuew)
        set(_d_cycles_have_gpu TRUE)
    else()
        set(_d_cycles_have_gpu FALSE)
    endif()
    foreach(_lib IN LISTS _cycles_static_libs)
        if(NOT EXISTS "${CYCLES_BUILD_DIR}/lib/lib${_lib}.a")
            message(FATAL_ERROR
                "Missing ${CYCLES_BUILD_DIR}/lib/lib${_lib}.a — "
                "re-run tools/build_cycles_libs.sh --reconfigure")
        endif()
        add_library(${_lib} STATIC IMPORTED GLOBAL)
        set_target_properties(${_lib} PROPERTIES
            IMPORTED_LOCATION "${CYCLES_BUILD_DIR}/lib/lib${_lib}.a")
    endforeach()

    # ---- Third-party libs from extern/blender/lib/linux_x64 ----------
    # We mirror what extern/blender/build_cycles/bin/cycles actually
    # links to (verified via ldd). Shared libs are referenced by their
    # versioned soname so the runtime dynamic loader doesn't need a
    # rebuild-the-symlinks dance — the unversioned .so symlinks point
    # at the same files. Static libs (.a) are direct.
    function(d_cycles_pick_so var path)
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR "shared library not found: ${path}")
        endif()
        set(${var} "${path}" PARENT_SCOPE)
    endfunction()

    d_cycles_pick_so(_OIIO     "${LIBDIR}/openimageio/lib/libOpenImageIO.so")
    d_cycles_pick_so(_OIIO_UTL "${LIBDIR}/openimageio/lib/libOpenImageIO_Util.so")
    d_cycles_pick_so(_OCIO     "${LIBDIR}/opencolorio/lib/libOpenColorIO.so")
    d_cycles_pick_so(_TBB      "${LIBDIR}/tbb/lib/libtbb.so")
    d_cycles_pick_so(_EMBREE   "${LIBDIR}/embree/lib/libembree4.so")
    d_cycles_pick_so(_OIDN     "${LIBDIR}/openimagedenoise/lib/libOpenImageDenoise.so")
    d_cycles_pick_so(_OPENEXR  "${LIBDIR}/openexr/lib/libOpenEXR.so")
    d_cycles_pick_so(_OEXRCORE "${LIBDIR}/openexr/lib/libOpenEXRCore.so")
    d_cycles_pick_so(_IEX      "${LIBDIR}/openexr/lib/libIex.so")
    d_cycles_pick_so(_ILMTHR   "${LIBDIR}/openexr/lib/libIlmThread.so")
    d_cycles_pick_so(_IMATH    "${LIBDIR}/imath/lib/libImath.so")
    d_cycles_pick_so(_OSD_CPU  "${LIBDIR}/opensubdiv/lib/libosdCPU.so")
    d_cycles_pick_so(_SYCL     "${LIBDIR}/dpcpp/lib/libsycl.so")

    # Static deps where Blender ships .a only.
    set(_PUGIXML "${LIBDIR}/pugixml/lib/libpugixml.a")
    set(_PNG  "${LIBDIR}/png/lib/libpng16.a")
    set(_JPEG "${LIBDIR}/jpeg/lib/libjpeg.a")
    set(_TIFF "${LIBDIR}/tiff/lib/libtiff.a")
    set(_OPENJPEG "${LIBDIR}/openjpeg/lib/libopenjp2.a")
    set(_WEBP "${LIBDIR}/webp/lib/libwebp.a")
    set(_ZLIB "${LIBDIR}/zlib/lib/libz.a")
    set(_ZSTD "${LIBDIR}/zstd/lib/libzstd.a")

    # ---- cyclesc target wiring ---------------------------------------
    # intern/atomic is sibling to intern/cycles; util/atomic.h does
    # `#include "atomic_ops.h"` so the dir containing that header must
    # be on the include path.
    target_include_directories(cyclesc PRIVATE
        "${BLENDER_SRC}/intern/cycles"
        "${BLENDER_SRC}/intern/atomic"
        "${BLENDER_SRC}/intern/guardedalloc"
        "${BLENDER_SRC}/extern/glog/include"
        "${BLENDER_SRC}/extern/gflags/src"
        "${BLENDER_SRC}/extern/cuew/include"
        "${LIBDIR}/openimageio/include"
        "${LIBDIR}/openexr/include"
        "${LIBDIR}/openexr/include/OpenEXR"
        "${LIBDIR}/imath/include"
        "${LIBDIR}/imath/include/Imath"
        "${LIBDIR}/opencolorio/include"
        "${LIBDIR}/embree/include"
        "${LIBDIR}/openimagedenoise/include"
        "${LIBDIR}/tbb/include"
        "${LIBDIR}/opensubdiv/include"
        "${LIBDIR}/pugixml/include"
    )
    # These two definitions act as inline `namespace ccl {` / `}` so
    # Cycles headers compile in our TU. Match Blender's invocation:
    # -DCCL_NAMESPACE_BEGIN=namespace ccl { -DCCL_NAMESPACE_END=}
    target_compile_definitions(cyclesc PRIVATE
        CYCLES_AVAILABLE=1
        "CCL_NAMESPACE_BEGIN=namespace ccl {"
        "CCL_NAMESPACE_END=}"
        # Cycles util/log.h pulls glog; the LOG/CHECK macros work in
        # both with-logging and stubbed forms. Cycles itself was built
        # WITH_CYCLES_LOGGING=ON, so we follow suit for ABI parity.
        WITH_CYCLES_LOGGING
        "CYCLES_GFLAGS_NAMESPACE="
    )
    target_compile_options(cyclesc PRIVATE -fno-trapping-math -fno-math-errno
                                            -fno-signed-zeros -ffp-contract=fast
                                            -fno-signaling-nans -fno-rounding-math)

    # Static lib link order matters; group resolves circular deps.
    set(_d_cycles_group_libs
        cycles_session cycles_scene cycles_kernel cycles_device
        cycles_integrator cycles_bvh cycles_subd cycles_graph cycles_util
        bf_intern_libc_compat bf_intern_guardedalloc bf_intern_sky
        extern_glog extern_gflags
    )
    if(_d_cycles_have_gpu)
        list(APPEND _d_cycles_group_libs extern_cuew)
    endif()

    target_link_libraries(cyclesc PRIVATE
        -Wl,--start-group ${_d_cycles_group_libs} -Wl,--end-group
        ${_OIIO} ${_OIIO_UTL}
        ${_OCIO}
        ${_EMBREE}
        ${_OIDN}
        ${_OPENEXR} ${_OEXRCORE} ${_IEX} ${_ILMTHR} ${_IMATH}
        ${_OSD_CPU}
        ${_SYCL}
        ${_PUGIXML}
        ${_TBB}
        ${_PNG} ${_JPEG} ${_TIFF} ${_OPENJPEG} ${_WEBP}
        ${_ZLIB} ${_ZSTD}
        pthread m dl
    )

    # Make the precompiled .so libs findable at runtime without
    # LD_LIBRARY_PATH. Consumers of cyclesc still need their own RPATH
    # set, but for the static cyclesc.a this lets the example app link
    # via INTERFACE_LINK_DIRECTORIES.
    set(_d_cycles_runtime_dirs
        "${LIBDIR}/openimageio/lib"
        "${LIBDIR}/opencolorio/lib"
        "${LIBDIR}/embree/lib"
        "${LIBDIR}/openimagedenoise/lib"
        "${LIBDIR}/openexr/lib"
        "${LIBDIR}/imath/lib"
        "${LIBDIR}/opensubdiv/lib"
        "${LIBDIR}/dpcpp/lib"
        "${LIBDIR}/tbb/lib"
    )
    set_target_properties(cyclesc PROPERTIES
        INTERFACE_LINK_DIRECTORIES "${_d_cycles_runtime_dirs}")
    # Encode the RPATH onto cyclesc itself so any test executable picks
    # it up via the static-lib transitive properties.
    foreach(_d IN LISTS _d_cycles_runtime_dirs)
        target_link_options(cyclesc INTERFACE "LINKER:-rpath,${_d}")
    endforeach()

    message(STATUS "D-Cycles: linking against ${CYCLES_BUILD_DIR}")
    message(STATUS "  LIBDIR = ${LIBDIR}")
endif()
