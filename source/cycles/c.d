/// D bindings for the D-Cycles C shim (`csrc/cyclesc.h`).
///
/// Mirrors the C header exactly — one struct per opaque handle, one
/// `extern (C)` declaration per entry point, no D-side wrapping yet.
/// Higher-level convenience layers (RAII handles, slice-based mesh
/// upload) belong in a sibling module so the raw surface stays
/// available for callers that need manual lifetime management.
///
/// Status: Phase 0 scaffold. Linkage works (stub `.cpp` returns
/// `CYC_ERR_UNSUPPORTED` for every entry); real implementation lands
/// in Phase 0c when Cycles is built from Blender source.
module cycles.c;

extern (C) @nogc nothrow:

// =====================================================================
// Status codes
// =====================================================================

alias cyc_status = int;

enum int CYC_OK                      =  0;
enum int CYC_ERR_INVALID_ARGUMENT    = -1;
enum int CYC_ERR_OUT_OF_MEMORY       = -2;
enum int CYC_ERR_DEVICE_INIT         = -3;
enum int CYC_ERR_NO_SUITABLE_DEVICE  = -4;
enum int CYC_ERR_SHADER_COMPILATION  = -5;
enum int CYC_ERR_FILE_IO             = -6;
enum int CYC_ERR_CANCELED            = -7;
enum int CYC_ERR_INTERNAL            = -8;
enum int CYC_ERR_UNSUPPORTED         = -9;

// =====================================================================
// Opaque handles
// =====================================================================

struct cyc_session_t;
struct cyc_scene_t;
struct cyc_mesh_t;
struct cyc_object_t;
struct cyc_light_t;
struct cyc_camera_t;
struct cyc_shader_t;
struct cyc_shader_node_t;

// =====================================================================
// Device enumeration
// =====================================================================

enum cyc_device_type : int {
    CPU     = 0,
    CUDA    = 1,
    OPTIX   = 2,
    HIP     = 3,
    METAL   = 4,
    ONEAPI  = 5,
}

struct cyc_device_info {
    cyc_device_type type;
    char[256]       name;
    int             index;
    int             supports_hw_rt;
}

cyc_status cyc_devices_query(cyc_device_info* out_devices, int max_devices, int* out_count);

// =====================================================================
// Session
// =====================================================================

struct cyc_session_params {
    cyc_device_type device_type;
    int             device_index;
    int             samples;
    int             threads;
    int             tile_size;
    int             use_denoiser;
    int             interactive;
}

cyc_status   cyc_session_create(const cyc_session_params* params, cyc_session_t** out_session);
void         cyc_session_destroy(cyc_session_t* session);
cyc_scene_t* cyc_session_scene(cyc_session_t* session);
cyc_status   cyc_session_start(cyc_session_t* session);
cyc_status   cyc_session_cancel(cyc_session_t* session);
cyc_status   cyc_session_wait(cyc_session_t* session);
cyc_status   cyc_session_progress(cyc_session_t* session, float* out_progress);
cyc_status   cyc_session_reset(cyc_session_t* session, int width, int height);

// Interactive-mode scene sync API. Wrap scene mutation in
// try_lock/unlock pairs to safely modify scene while session worker
// thread is running. After unlocking, call cyc_session_reset +
// cyc_session_start to apply changes — no destroy+recreate needed.
int          cyc_session_scene_try_lock(cyc_session_t* session);
void         cyc_session_scene_unlock(cyc_session_t* session);
void         cyc_session_set_pause(cyc_session_t* session, int paused);
void         cyc_session_set_samples(cyc_session_t* session, int samples);
int          cyc_session_ready_to_reset(cyc_session_t* session);

// Display driver — progressive IPR readback.
//
// CapturingDisplayDriver is installed at session create. Cycles writes
// half4 RGBA pixels via DisplayDriver protocol; host polls
// cyc_session_display_version (atomic uint64 counter, bumps on each
// frame end) and reads pixels via cyc_session_display_read_pixels
// (half→float on copy).
// Optional GL-PBO interop: cyc_session_display_bind_gl_pbo registers
// a host-owned PBO so Cycles writes directly to GPU.
cyc_status cyc_session_display_read_pixels(cyc_session_t* session,
                                           float* out_rgba,
                                           int width, int height);
ulong      cyc_session_display_version(cyc_session_t* session);
cyc_status cyc_session_display_bind_gl_pbo(cyc_session_t* session,
                                           ulong gl_pbo_id,
                                           ulong size_bytes);
int        cyc_session_display_cpu_path_used(cyc_session_t* session);

// =====================================================================
// Scene
// =====================================================================

cyc_status cyc_scene_clear(cyc_scene_t* scene);

// =====================================================================
// Mesh
// =====================================================================

cyc_status cyc_mesh_create(cyc_scene_t* scene, cyc_mesh_t** out_mesh);
cyc_status cyc_mesh_destroy(cyc_scene_t* scene, cyc_mesh_t* mesh);
cyc_status cyc_mesh_set_verts(cyc_mesh_t* mesh, const(float)* verts, int num_verts);
cyc_status cyc_mesh_set_triangles(cyc_mesh_t* mesh,
                                  const(int)* tri_indices, int num_tris,
                                  const(int)* smooth_flags);
cyc_status cyc_mesh_set_normals(cyc_mesh_t* mesh, const(float)* normals, int num_normals);
cyc_status cyc_mesh_set_uvs(cyc_mesh_t* mesh,
                            const(float)* uvs, int num_uvs,
                            const(char)* uv_layer_name);

// =====================================================================
// Object
// =====================================================================

cyc_status cyc_object_create(cyc_scene_t* scene, cyc_object_t** out_object);
cyc_status cyc_object_destroy(cyc_scene_t* scene, cyc_object_t* object);
cyc_status cyc_object_set_mesh(cyc_object_t* object, cyc_mesh_t* mesh);
cyc_status cyc_object_set_transform(cyc_object_t* object, const(float)* matrix4x4);
cyc_status cyc_object_set_shader(cyc_object_t* object, cyc_shader_t* shader);
cyc_status cyc_object_set_visible(cyc_object_t* object, int visible);

// =====================================================================
// Light
// =====================================================================

enum cyc_light_type : int {
    POINT       = 0,
    SUN         = 1,
    SPOT        = 2,
    AREA_RECT   = 3,
    AREA_DISK   = 4,
    BACKGROUND  = 5,
}

cyc_status cyc_light_create(cyc_scene_t* scene, cyc_light_type type, cyc_light_t** out_light);
cyc_status cyc_light_destroy(cyc_scene_t* scene, cyc_light_t* light);
cyc_status cyc_light_set_transform(cyc_light_t* light, const(float)* matrix4x4);
cyc_status cyc_light_set_color(cyc_light_t* light, float r, float g, float b);
cyc_status cyc_light_set_intensity(cyc_light_t* light, float watts_per_m2);
cyc_status cyc_light_set_size(cyc_light_t* light, float size_x, float size_y);
cyc_status cyc_light_set_spot_angle(cyc_light_t* light, float angle_rad, float blend);
cyc_status cyc_light_set_sun_angle(cyc_light_t* light, float angle_rad);

// =====================================================================
// Camera
// =====================================================================

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
cyc_status cyc_camera_set_dof(cyc_camera_t* camera, float focal_distance, float aperture_size);
cyc_status cyc_scene_set_active_camera(cyc_scene_t* scene, cyc_camera_t* camera);

// =====================================================================
// Shader
// =====================================================================

enum cyc_node_type : int {
    PRINCIPLED_BSDF      = 0,
    DIFFUSE_BSDF         = 1,
    GLOSSY_BSDF          = 2,
    EMISSION             = 3,
    OUTPUT_MATERIAL      = 4,
    IMAGE_TEXTURE        = 5,
    NOISE_TEXTURE        = 6,
    VORONOI_TEXTURE      = 7,
    GRADIENT_TEXTURE     = 8,
    MIX_RGB              = 9,
    MATH                 = 10,
    TEXTURE_COORDINATE   = 11,
    MAPPING              = 12,
    NORMAL_MAP           = 13,
    BUMP                 = 14,
}

cyc_status cyc_shader_create(cyc_scene_t* scene, cyc_shader_t** out_shader);
cyc_status cyc_shader_destroy(cyc_scene_t* scene, cyc_shader_t* shader);
cyc_status cyc_shader_create_principled(cyc_scene_t* scene, cyc_shader_t** out_shader);

cyc_status cyc_shader_set_principled_base_color(cyc_shader_t* shader, float r, float g, float b);
cyc_status cyc_shader_set_principled_roughness(cyc_shader_t* shader, float roughness);
cyc_status cyc_shader_set_principled_metallic(cyc_shader_t* shader, float metallic);
cyc_status cyc_shader_set_principled_emission(cyc_shader_t* shader, float r, float g, float b, float strength);
cyc_status cyc_shader_set_principled_specular(cyc_shader_t* shader, float ior);
cyc_status cyc_shader_set_principled_transmission(cyc_shader_t* shader, float transmission);

cyc_status cyc_shader_add_node(cyc_shader_t* shader, cyc_node_type type, cyc_shader_node_t** out_node);
cyc_status cyc_shader_connect(cyc_shader_t* shader,
                              cyc_shader_node_t* src, const(char)* src_socket,
                              cyc_shader_node_t* dst, const(char)* dst_socket);
cyc_status cyc_shader_node_set_input_float(cyc_shader_node_t* node, const(char)* socket, float value);
cyc_status cyc_shader_node_set_input_color(cyc_shader_node_t* node, const(char)* socket, float r, float g, float b);
cyc_status cyc_shader_node_set_input_vector(cyc_shader_node_t* node, const(char)* socket, float x, float y, float z);

// =====================================================================
// Render output
// =====================================================================

cyc_status cyc_session_read_framebuffer(cyc_session_t* session,
                                        float* out_pixels, int width, int height);
cyc_status cyc_session_save_image(cyc_session_t* session, const(char)* file_path);
