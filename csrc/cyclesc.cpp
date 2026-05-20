/*
 * cyclesc.cpp — Phase 0 stub implementation.
 *
 * Every entry point returns CYC_ERR_UNSUPPORTED so the shim library links
 * cleanly and the D side can compile-test against the real surface.
 * Real implementation lives behind #ifdef CYCLES_AVAILABLE once Cycles is
 * built from Blender source — see CMakeLists.txt and README.md.
 */

#include "cyclesc.h"

#include <string.h>

#ifndef CYCLES_AVAILABLE

/* ---------------------------------------------------------------------
 * Scaffold-only stubs. Every function returns CYC_ERR_UNSUPPORTED.
 * Build with CYCLES_AVAILABLE=1 once Cycles is wired up (Phase 0c).
 * --------------------------------------------------------------------- */

#define STUB_RETURN_NONFAIL_DEVICES_QUERY(out_count) \
    do { if (out_count) *out_count = 0; return CYC_OK; } while (0)

extern "C" {

cyc_status cyc_devices_query(cyc_device_info* /*out*/, int /*max*/, int* out_count) {
    STUB_RETURN_NONFAIL_DEVICES_QUERY(out_count);
}

cyc_status cyc_session_create(const cyc_session_params*, cyc_session_t**)         { return CYC_ERR_UNSUPPORTED; }
void       cyc_session_destroy(cyc_session_t*)                                    { }
cyc_scene_t* cyc_session_scene(cyc_session_t*)                                    { return nullptr; }
cyc_status cyc_session_start(cyc_session_t*)                                      { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_session_cancel(cyc_session_t*)                                     { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_session_wait(cyc_session_t*)                                       { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_session_progress(cyc_session_t*, float*)                           { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_session_reset(cyc_session_t*, int, int)                            { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_scene_clear(cyc_scene_t*)                                          { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_mesh_create(cyc_scene_t*, cyc_mesh_t**)                            { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_mesh_destroy(cyc_scene_t*, cyc_mesh_t*)                            { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_mesh_set_verts(cyc_mesh_t*, const float*, int)                     { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_mesh_set_triangles(cyc_mesh_t*, const int*, int, const int*)       { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_mesh_set_normals(cyc_mesh_t*, const float*, int)                   { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_mesh_set_uvs(cyc_mesh_t*, const float*, int, const char*)          { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_object_create(cyc_scene_t*, cyc_object_t**)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_object_destroy(cyc_scene_t*, cyc_object_t*)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_object_set_mesh(cyc_object_t*, cyc_mesh_t*)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_object_set_transform(cyc_object_t*, const float*)                  { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_object_set_shader(cyc_object_t*, cyc_shader_t*)                    { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_object_set_visible(cyc_object_t*, int)                             { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_light_create(cyc_scene_t*, cyc_light_type, cyc_light_t**)          { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_destroy(cyc_scene_t*, cyc_light_t*)                          { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_set_transform(cyc_light_t*, const float*)                    { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_set_color(cyc_light_t*, float, float, float)                 { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_set_intensity(cyc_light_t*, float)                           { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_set_size(cyc_light_t*, float, float)                         { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_set_spot_angle(cyc_light_t*, float, float)                   { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_light_set_sun_angle(cyc_light_t*, float)                           { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_camera_create_perspective(cyc_scene_t*, cyc_camera_t**)            { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_create_ortho(cyc_scene_t*, cyc_camera_t**)                  { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_destroy(cyc_scene_t*, cyc_camera_t*)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_lookat(cyc_camera_t*, float, float, float, float, float, float, float, float, float)  { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_set_fov(cyc_camera_t*, float)                               { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_set_aspect(cyc_camera_t*, float)                            { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_set_clip(cyc_camera_t*, float, float)                       { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_camera_set_dof(cyc_camera_t*, float, float)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_scene_set_active_camera(cyc_scene_t*, cyc_camera_t*)               { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_shader_create(cyc_scene_t*, cyc_shader_t**)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_destroy(cyc_scene_t*, cyc_shader_t*)                        { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_create_principled(cyc_scene_t*, cyc_shader_t**)             { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_set_principled_base_color(cyc_shader_t*, float, float, float)            { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_set_principled_roughness(cyc_shader_t*, float)              { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_set_principled_metallic(cyc_shader_t*, float)               { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_set_principled_emission(cyc_shader_t*, float, float, float, float)       { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_set_principled_specular(cyc_shader_t*, float)               { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_set_principled_transmission(cyc_shader_t*, float)           { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_shader_add_node(cyc_shader_t*, cyc_node_type, cyc_shader_node_t**) { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_connect(cyc_shader_t*, cyc_shader_node_t*, const char*,
                              cyc_shader_node_t*, const char*)                    { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_node_set_input_float(cyc_shader_node_t*, const char*, float)             { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_node_set_input_color(cyc_shader_node_t*, const char*, float, float, float)  { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_shader_node_set_input_vector(cyc_shader_node_t*, const char*, float, float, float) { return CYC_ERR_UNSUPPORTED; }

cyc_status cyc_session_read_framebuffer(cyc_session_t*, float*, int, int)         { return CYC_ERR_UNSUPPORTED; }
cyc_status cyc_session_save_image(cyc_session_t*, const char*)                    { return CYC_ERR_UNSUPPORTED; }

} /* extern "C" */

#endif /* !CYCLES_AVAILABLE */
