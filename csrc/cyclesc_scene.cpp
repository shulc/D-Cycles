/*
 * cyclesc_scene.cpp — scene-level operations.
 */

#include "cyclesc_internal.h"

#include "scene/scene.h"
#include "scene/camera.h"

using namespace cyc_internal;

extern "C"
cyc_status cyc_scene_clear(cyc_scene_t *h)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    /* Not supported in Phase 0c — we recreate sessions instead.
     * Full ccl::Scene::reset() needs careful handling to avoid
     * tearing down the default camera/integrator. */
    return CYC_ERR_UNSUPPORTED;
}

extern "C"
cyc_status cyc_scene_set_active_camera(cyc_scene_t *h, cyc_camera_t *cam_h)
{
    if (!h || !cam_h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Scene  *scene  = to_scene(h);
    ccl::Camera *camera = to_camera(cam_h);
    scene->camera = camera;
    camera->need_flags_update  = true;
    camera->need_device_update = true;
    return CYC_OK;
}
