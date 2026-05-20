/*
 * cyclesc_camera.cpp — Camera. Scene already owns a default camera at
 * scene->camera; we let callers either drive that one or create new
 * cameras and switch the active one via cyc_scene_set_active_camera.
 */

#include "cyclesc_internal.h"

#include "kernel/types.h"  /* CameraType */
#include "scene/camera.h"
#include "scene/scene.h"
#include "util/transform.h"
#include "util/types.h"

#include <cmath>

using namespace cyc_internal;

static ccl::Camera *make_camera(ccl::Scene *scene, ccl::CameraType type)
{
    /* Prefer reusing the default camera: it's pre-installed in
     * Scene::camera and integrating new ones requires explicit
     * activation. For Phase 0c we reuse the existing one if it's
     * still at defaults; otherwise we create a new Camera node. */
    ccl::Camera *cam = scene->camera;
    if (!cam) {
        cam = scene->create_node<ccl::Camera>();
        scene->camera = cam;
    }
    cam->set_camera_type(type);
    return cam;
}

extern "C"
cyc_status cyc_camera_create_perspective(cyc_scene_t *scene_h, cyc_camera_t **out)
{
    if (!scene_h || !out) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Camera *cam = make_camera(to_scene(scene_h), ccl::CAMERA_PERSPECTIVE);
    *out = from_camera(cam);
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_create_ortho(cyc_scene_t *scene_h, cyc_camera_t **out)
{
    if (!scene_h || !out) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Camera *cam = make_camera(to_scene(scene_h), ccl::CAMERA_ORTHOGRAPHIC);
    *out = from_camera(cam);
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_destroy(cyc_scene_t *scene_h, cyc_camera_t *h)
{
    (void)scene_h; (void)h;
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_lookat(cyc_camera_t *h,
                             float ex, float ey, float ez,
                             float tx, float ty, float tz,
                             float ux, float uy, float uz)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    /* Cycles' perspective projection (util/projection.h) maps camera
     * -space +Z to screen depth, so its camera looks down +Z (LH-like
     * convention). The camera-to-world matrix's column 2 must point
     * FROM eye TOWARDS target — i.e. forward = normalize(target - eye).
     * X column = forward x up; Y column = X x forward. Translation
     * column = eye position. */
    auto normalize = [](float x, float y, float z) {
        const float n = std::sqrt(x*x + y*y + z*z);
        return n > 0 ? ccl::make_float3(x/n, y/n, z/n) : ccl::make_float3(0,0,1);
    };
    auto cross = [](ccl::float3 a, ccl::float3 b) {
        return ccl::make_float3(a.y*b.z - a.z*b.y,
                                a.z*b.x - a.x*b.z,
                                a.x*b.y - a.y*b.x);
    };
    const ccl::float3 z_axis = normalize(tx - ex, ty - ey, tz - ez);
    const ccl::float3 up_raw = ccl::make_float3(ux, uy, uz);
    /* LH frame: right = up x forward; up_corrected = forward x right. */
    ccl::float3 x_axis = cross(up_raw, z_axis);
    {
        const float n = std::sqrt(x_axis.x*x_axis.x + x_axis.y*x_axis.y + x_axis.z*x_axis.z);
        if (n > 0) x_axis = ccl::make_float3(x_axis.x/n, x_axis.y/n, x_axis.z/n);
    }
    const ccl::float3 y_axis = cross(z_axis, x_axis);

    ccl::Transform tfm;
    tfm.x = ccl::make_float4(x_axis.x, y_axis.x, z_axis.x, ex);
    tfm.y = ccl::make_float4(x_axis.y, y_axis.y, z_axis.y, ey);
    tfm.z = ccl::make_float4(x_axis.z, y_axis.z, z_axis.z, ez);

    ccl::Camera *cam = to_camera(h);
    cam->set_matrix(tfm);
    cam->need_flags_update  = true;
    cam->need_device_update = true;
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_set_fov(cyc_camera_t *h, float fov_rad)
{
    if (!h || fov_rad <= 0.0f) return CYC_ERR_INVALID_ARGUMENT;
    to_camera(h)->set_fov(fov_rad);
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_set_aspect(cyc_camera_t *h, float /*aspect*/)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    /* Aspect is derived from full_width / full_height. We set those via
     * cyc_session_reset; explicit override here is unnecessary. */
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_set_clip(cyc_camera_t *h, float near_plane, float far_plane)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Camera *cam = to_camera(h);
    cam->set_nearclip(near_plane);
    cam->set_farclip(far_plane);
    return CYC_OK;
}

extern "C"
cyc_status cyc_camera_set_dof(cyc_camera_t *h, float focal_distance, float aperture_size)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Camera *cam = to_camera(h);
    cam->set_focaldistance(focal_distance);
    cam->set_aperturesize(aperture_size);
    return CYC_OK;
}
