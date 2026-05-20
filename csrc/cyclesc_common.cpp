/*
 * cyclesc_common.cpp — one-time Cycles process init, transform helper.
 */

#include "cyclesc_internal.h"

#include "util/log.h"
#include "util/path.h"

#include <cstdlib>

namespace cyc_internal {

void ensure_global_init()
{
    static std::once_flag once;
    std::call_once(once, [] {
        ccl::util_logging_init("cyclesc");

        /* Cycles uses path_get("lib/<kernel>.cubin.zst" / ".ptx.zst") to
         * locate GPU kernel binaries at runtime. Without an explicit
         * path, it falls back to the directory of the running
         * executable + "lib/". For the D-Cycles smoke tests the
         * binaries live next to the executable but the kernels live in
         * extern/blender/build_cycles/intern/cycles/ — let the caller
         * point us at that root via CYCLESC_KERNEL_PATH. */
        const char *kp = std::getenv("CYCLESC_KERNEL_PATH");
        if (kp && *kp) {
            ccl::path_init(kp, kp);
        } else {
            ccl::path_init();
        }
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
