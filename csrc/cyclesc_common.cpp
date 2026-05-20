/*
 * cyclesc_common.cpp — one-time Cycles process init, transform helper.
 */

#include "cyclesc_internal.h"

#include "util/log.h"
#include "util/path.h"

namespace cyc_internal {

void ensure_global_init()
{
    static std::once_flag once;
    std::call_once(once, [] {
        ccl::util_logging_init("cyclesc");
        ccl::path_init();
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
