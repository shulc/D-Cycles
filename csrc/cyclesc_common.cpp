/*
 * cyclesc_common.cpp — one-time Cycles process init, transform helper.
 */

#include "cyclesc_internal.h"

#include "util/log.h"
#include "util/path.h"

#include <cstdlib>

/* cuew (CUDA EWrapper) — wraps libcuda.so via dlopen + dlsym.
 * Without an explicit `cuewInit(CUEW_INIT_CUDA)` the function pointers
 * stay NULL. OIDN's CUDA device probe (triggered during
 * Device::available_devices via oidnIsCUDADeviceSupported, which
 * dlopen's libOpenImageDenoise_device_cuda.so and calls cuInit) lands
 * on the null pointer and segfaults.
 *
 * LDC2-linked binaries usually avoid the crash because OIDN's runtime
 * dependency resolution happens to find libcuda.so's real cuInit
 * before the cuew null wrapper — masking the bug. DMD-linked binaries
 * hit the null wrapper first and crash. Pre-initialising cuew here
 * is the compiler-independent fix. */
#ifdef WITH_CYCLES_CUEW
extern "C" {
#include "cuew.h"
}
#endif

namespace cyc_internal {

void ensure_global_init()
{
    static std::once_flag once;
    std::call_once(once, [] {
        ccl::util_logging_init("cyclesc");

        /* Cycles' path_get(sub) returns <cached_path>/sub. Different
         * device backends ask for different subdirs:
         *   - CUDA/OptiX: path_get("lib/kernel_*.ptx.zst") — precompiled.
         *   - Metal:      path_get("source/kernel/device/metal/kernel.metal")
         *                 — JIT-compiled at runtime from source.
         * Both must resolve, so cached_path is a unified runtime root.
         * tools/build_cycles_libs.sh stages this layout at
         * ${CYCLES_BUILD_DIR}/bin (lib/ + source/ symlink), and CMake
         * bakes that absolute path into CYCLESC_DEFAULT_KERNEL_PATH as
         * the dev-tree fallback. CYCLESC_KERNEL_PATH env var still
         * wins — required for deployed binaries where build_cycles
         * doesn't exist alongside the running executable. */
        const char *kp = std::getenv("CYCLESC_KERNEL_PATH");
        if (kp && *kp) {
            ccl::path_init(kp, kp);
        }
#ifdef CYCLESC_DEFAULT_KERNEL_PATH
        else {
            ccl::path_init(CYCLESC_DEFAULT_KERNEL_PATH, CYCLESC_DEFAULT_KERNEL_PATH);
        }
#else
        else {
            ccl::path_init();
        }
#endif

#ifdef WITH_CYCLES_CUEW
        /* Load libcuda.so + libnvrtc.so via cuew so the function
         * pointers cuInit/cuCtxCreate/... resolve to the real driver
         * symbols. Non-zero return is non-fatal — Cycles falls back
         * to other devices if CUDA isn't usable. The companion fix
         * (linker flag --exclude-libs=libextern_cuew.a in dub.json)
         * keeps cuew's globals from being dynamically exported, so
         * OIDN's dlopen'd CUDA module finds libcuda.so's real cuInit
         * instead of cuew's NULL-initialised function-pointer
         * variable. */
        (void) cuewInit(CUEW_INIT_CUDA | CUEW_INIT_NVRTC);
#endif
    });
}

ccl::Transform mat4_to_transform(const float *m)
{
    /* Caller passes a row-major 4x4 matrix. ccl::Transform stores three
     * rows of (float4); the last row is implicitly (0,0,0,1) and the
     * code reads each row as a float4 from .x/.y/.z/.w members. */
    ccl::Transform t;
    t.x.x = m[0];  t.x.y = m[1];  t.x.z = m[2];  t.x.w = m[3];
    t.y.x = m[4];  t.y.y = m[5];  t.y.z = m[6];  t.y.w = m[7];
    t.z.x = m[8];  t.z.y = m[9];  t.z.z = m[10]; t.z.w = m[11];
    return t;
}

}  // namespace cyc_internal
