/*
 * cyclesc.h — C shim over Cycles X (Blender's path tracer) C++ API.
 *
 * Design goals:
 *   - Opaque handles only; D side never touches C++ types.
 *   - Functions group by Cycles concept: session, scene, mesh, object,
 *     shader, light, camera, render.
 *   - Mirror Cycles X (Blender 4.x) structure; don't invent new abstractions.
 *   - Status codes (CYC_OK / CYC_ERR_*) consistent across all functions.
 *
 * Status: Phase 0 scaffold. Function declarations represent the intended
 * surface; implementations live in csrc/*.cpp and are TODO until Cycles
 * itself is built from source. See README.md for build instructions.
 *
 * Naming convention:
 *   cyc_<concept>_<action>          one-shot operation
 *   cyc_<concept>_create / destroy  lifecycle
 *   cyc_<concept>_set_<param>       configuration
 *   cyc_<concept>_get_<param>       readback
 */

#ifndef CYCLESC_H
#define CYCLESC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Status codes
 * ==================================================================== */

typedef int cyc_status;

#define CYC_OK                       0
#define CYC_ERR_INVALID_ARGUMENT    -1
#define CYC_ERR_OUT_OF_MEMORY       -2
#define CYC_ERR_DEVICE_INIT         -3
#define CYC_ERR_NO_SUITABLE_DEVICE  -4
#define CYC_ERR_SHADER_COMPILATION  -5
#define CYC_ERR_FILE_IO             -6
#define CYC_ERR_CANCELED            -7
#define CYC_ERR_INTERNAL            -8
#define CYC_ERR_UNSUPPORTED         -9

/* ====================================================================
 * Opaque handles
 * ==================================================================== */

typedef struct cyc_session_t      cyc_session_t;
typedef struct cyc_scene_t        cyc_scene_t;
typedef struct cyc_mesh_t         cyc_mesh_t;
typedef struct cyc_object_t       cyc_object_t;
typedef struct cyc_light_t        cyc_light_t;
typedef struct cyc_camera_t       cyc_camera_t;
typedef struct cyc_shader_t       cyc_shader_t;
typedef struct cyc_shader_node_t  cyc_shader_node_t;

/* ====================================================================
 * Device enumeration
 *
 * Call cyc_devices_query first to learn what's available, then pass the
 * chosen device descriptor to session creation.
 * ==================================================================== */

typedef enum {
    CYC_DEVICE_CPU      = 0,
    CYC_DEVICE_CUDA     = 1,    /* NVIDIA via CUDA              */
    CYC_DEVICE_OPTIX    = 2,    /* NVIDIA via OptiX (RT cores) */
    CYC_DEVICE_HIP      = 3,    /* AMD via HIP                  */
    CYC_DEVICE_METAL    = 4,    /* Apple Silicon / AMD on macOS */
    CYC_DEVICE_ONEAPI   = 5,    /* Intel Arc                    */
} cyc_device_type;

typedef struct {
    cyc_device_type type;
    char            name[256];         /* human-readable, e.g. "NVIDIA RTX 3070 Ti" */
    int             index;             /* device index within its type */
    int             supports_hw_rt;    /* RT cores / RDNA2+ / Metal3 hw raytracing */
} cyc_device_info;

cyc_status cyc_devices_query(cyc_device_info* out_devices, int max_devices, int* out_count);

/* ====================================================================
 * Session — owns a Scene and orchestrates rendering.
 * ==================================================================== */

typedef struct {
    cyc_device_type device_type;
    int             device_index;      /* per-type index; 0 = first */
    int             samples;           /* convergence target */
    int             threads;           /* 0 = auto */
    int             tile_size;         /* in pixels; 0 = auto */
    int             use_denoiser;      /* 1 = OIDN/OptiX denoiser on final */
    int             interactive;       /* 1 = progressive IPR mode */
} cyc_session_params;

cyc_status cyc_session_create(const cyc_session_params* params, cyc_session_t** out_session);
void       cyc_session_destroy(cyc_session_t* session);

/* Borrowed pointer; lifetime tied to session. */
cyc_scene_t* cyc_session_scene(cyc_session_t* session);

/* Render lifecycle — non-blocking. */
cyc_status cyc_session_start(cyc_session_t* session);
cyc_status cyc_session_cancel(cyc_session_t* session);
cyc_status cyc_session_wait(cyc_session_t* session);          /* block until done/cancel */

/* Progress: 0.0 .. 1.0 */
cyc_status cyc_session_progress(cyc_session_t* session, float* out_progress);

/* Reset render state — call after scene mutations during interactive mode. */
cyc_status cyc_session_reset(cyc_session_t* session, int width, int height);

/* True if calling cyc_session_reset right now will not stall waiting for
 * an in-flight device sample to finish. Cycles' interactive IPR loop
 * uses this as a gate: when false, the host should keep accumulating
 * the current camera/scene a beat longer (or set_pause+retry) instead
 * of forcing a reset and burning a frame on the device wait. */
int  cyc_session_ready_to_reset(cyc_session_t* session);

/* ====================================================================
 * Scene — graph of objects, lights, camera, materials.
 *
 * Scene is owned by Session; create/destroy through cyc_session_*.
 * Building a Scene is "diff-able": you may add/remove/update items and
 * call cyc_session_reset to apply changes during IPR.
 * ==================================================================== */

cyc_status cyc_scene_clear(cyc_scene_t* scene);

/* ====================================================================
 * Mesh — geometry resource. Attach to Scene; refer to from Object(s).
 * Many Objects can share one Mesh.
 * ==================================================================== */

cyc_status cyc_mesh_create(cyc_scene_t* scene, cyc_mesh_t** out_mesh);
cyc_status cyc_mesh_destroy(cyc_scene_t* scene, cyc_mesh_t* mesh);

/* Vertex positions: tightly packed float[3] * num_verts. */
cyc_status cyc_mesh_set_verts(cyc_mesh_t* mesh,
                              const float* verts, int num_verts);

/* Triangle indices: int[3] * num_tris. Each entry is an index into the
 * vertex array. Per-triangle smooth flag (0 = flat shading, 1 = smooth). */
cyc_status cyc_mesh_set_triangles(cyc_mesh_t* mesh,
                                  const int* tri_indices, int num_tris,
                                  const int* smooth_flags /* may be NULL */);

/* Optional per-vertex normals (otherwise computed). */
cyc_status cyc_mesh_set_normals(cyc_mesh_t* mesh,
                                const float* normals, int num_normals);

/* Optional UVs (float[2] per vertex). */
cyc_status cyc_mesh_set_uvs(cyc_mesh_t* mesh,
                            const float* uvs, int num_uvs,
                            const char* uv_layer_name /* "UVMap" if NULL */);

/* ====================================================================
 * Object — instance of a Mesh in the Scene with transform + shader.
 * ==================================================================== */

cyc_status cyc_object_create(cyc_scene_t* scene, cyc_object_t** out_object);
cyc_status cyc_object_destroy(cyc_scene_t* scene, cyc_object_t* object);

cyc_status cyc_object_set_mesh(cyc_object_t* object, cyc_mesh_t* mesh);

/* Row-major 4x4 transform (column 3 = translation). */
cyc_status cyc_object_set_transform(cyc_object_t* object, const float* matrix4x4);

/* Material assignment. NULL = default. */
cyc_status cyc_object_set_shader(cyc_object_t* object, cyc_shader_t* shader);

cyc_status cyc_object_set_visible(cyc_object_t* object, int visible);

/* ====================================================================
 * Light
 * ==================================================================== */

typedef enum {
    CYC_LIGHT_POINT       = 0,
    CYC_LIGHT_SUN         = 1,     /* directional, parallel rays */
    CYC_LIGHT_SPOT        = 2,
    CYC_LIGHT_AREA_RECT   = 3,
    CYC_LIGHT_AREA_DISK   = 4,
    CYC_LIGHT_BACKGROUND  = 5,     /* environment / world */
} cyc_light_type;

cyc_status cyc_light_create(cyc_scene_t* scene, cyc_light_type type, cyc_light_t** out_light);
cyc_status cyc_light_destroy(cyc_scene_t* scene, cyc_light_t* light);

cyc_status cyc_light_set_transform(cyc_light_t* light, const float* matrix4x4);
cyc_status cyc_light_set_color(cyc_light_t* light, float r, float g, float b);
cyc_status cyc_light_set_intensity(cyc_light_t* light, float watts_per_m2);

/* Type-specific. Calls are no-ops if light's type doesn't apply. */
cyc_status cyc_light_set_size(cyc_light_t* light, float size_x, float size_y);
cyc_status cyc_light_set_spot_angle(cyc_light_t* light, float angle_rad, float blend);
cyc_status cyc_light_set_sun_angle(cyc_light_t* light, float angle_rad);

/* ====================================================================
 * Camera
 * ==================================================================== */

cyc_status cyc_camera_create_perspective(cyc_scene_t* scene, cyc_camera_t** out_camera);
cyc_status cyc_camera_create_ortho(cyc_scene_t* scene, cyc_camera_t** out_camera);
cyc_status cyc_camera_destroy(cyc_scene_t* scene, cyc_camera_t* camera);

cyc_status cyc_camera_lookat(cyc_camera_t* camera,
                             float ex, float ey, float ez,
                             float tx, float ty, float tz,
                             float ux, float uy, float uz);

cyc_status cyc_camera_set_fov(cyc_camera_t* camera, float horizontal_fov_rad);
cyc_status cyc_camera_set_aspect(cyc_camera_t* camera, float width_over_height);
cyc_status cyc_camera_set_clip(cyc_camera_t* camera, float near_plane, float far_plane);

/* DoF — focal_distance in scene units; aperture_size in lens units. */
cyc_status cyc_camera_set_dof(cyc_camera_t* camera,
                              float focal_distance, float aperture_size);

cyc_status cyc_scene_set_active_camera(cyc_scene_t* scene, cyc_camera_t* camera);

/* ====================================================================
 * Shader — wraps a Cycles ShaderGraph. Build by adding nodes and
 * connecting them; the output node represents the final material.
 *
 * For minimum-viable triangle: skip the graph API and use
 * cyc_shader_create_principled() with direct parameter setters below.
 * The graph API is needed once vibe3d's Shader Tree compiles into
 * a Cycles node graph.
 * ==================================================================== */

cyc_status cyc_shader_create(cyc_scene_t* scene, cyc_shader_t** out_shader);
cyc_status cyc_shader_destroy(cyc_scene_t* scene, cyc_shader_t* shader);

/* Convenience: build a graph containing only Principled BSDF + Material
 * Output. Parameters apply directly. Sufficient for Phase 1 default
 * material; replace with explicit graph construction in Phase 2. */
cyc_status cyc_shader_create_principled(cyc_scene_t* scene, cyc_shader_t** out_shader);

cyc_status cyc_shader_set_principled_base_color(cyc_shader_t* shader, float r, float g, float b);
cyc_status cyc_shader_set_principled_roughness(cyc_shader_t* shader, float roughness);
cyc_status cyc_shader_set_principled_metallic(cyc_shader_t* shader, float metallic);
cyc_status cyc_shader_set_principled_emission(cyc_shader_t* shader, float r, float g, float b, float strength);
cyc_status cyc_shader_set_principled_specular(cyc_shader_t* shader, float ior);
cyc_status cyc_shader_set_principled_transmission(cyc_shader_t* shader, float transmission);

/* Generic node-graph API — Phase 2+. Not implemented in Phase 0 scaffold. */
typedef enum {
    CYC_NODE_PRINCIPLED_BSDF       = 0,
    CYC_NODE_DIFFUSE_BSDF          = 1,
    CYC_NODE_GLOSSY_BSDF           = 2,
    CYC_NODE_EMISSION              = 3,
    CYC_NODE_OUTPUT_MATERIAL       = 4,
    CYC_NODE_IMAGE_TEXTURE         = 5,
    CYC_NODE_NOISE_TEXTURE         = 6,
    CYC_NODE_VORONOI_TEXTURE       = 7,
    CYC_NODE_GRADIENT_TEXTURE      = 8,
    CYC_NODE_MIX_RGB               = 9,
    CYC_NODE_MATH                  = 10,
    CYC_NODE_TEXTURE_COORDINATE    = 11,
    CYC_NODE_MAPPING               = 12,
    CYC_NODE_NORMAL_MAP            = 13,
    CYC_NODE_BUMP                  = 14,
    /* extend as Shader Tree compiler needs */
} cyc_node_type;

cyc_status cyc_shader_add_node(cyc_shader_t* shader, cyc_node_type type, cyc_shader_node_t** out_node);
cyc_status cyc_shader_connect(cyc_shader_t* shader,
                              cyc_shader_node_t* src, const char* src_socket,
                              cyc_shader_node_t* dst, const char* dst_socket);
cyc_status cyc_shader_node_set_input_float(cyc_shader_node_t* node, const char* socket, float value);
cyc_status cyc_shader_node_set_input_color(cyc_shader_node_t* node, const char* socket, float r, float g, float b);
cyc_status cyc_shader_node_set_input_vector(cyc_shader_node_t* node, const char* socket, float x, float y, float z);

/* ====================================================================
 * Render output
 * ==================================================================== */

/* Read final RGBA framebuffer (float32 * 4 channels) after session completes.
 * Caller allocates: out_pixels must hold width*height*4*sizeof(float) bytes. */
cyc_status cyc_session_read_framebuffer(cyc_session_t* session,
                                        float* out_pixels,
                                        int width, int height);

/* Convenience: save current framebuffer to a PNG (8-bit) or EXR (float).
 * Format inferred from extension. */
cyc_status cyc_session_save_image(cyc_session_t* session, const char* file_path);

#ifdef __cplusplus
}
#endif

#endif /* CYCLESC_H */
