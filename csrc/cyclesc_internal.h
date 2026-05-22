/*
 * cyclesc_internal.h — internal C++ helpers shared by csrc/cyclesc_*.cpp.
 *
 * Only compiled when CYCLES_AVAILABLE is defined (real Cycles backend).
 * The D side never sees this header; it sees only opaque handles from
 * cyclesc.h.
 *
 * Design:
 *   - cyc_*_t handles cast directly to/from ccl pointers for Mesh, Object,
 *     Camera, Shader, ShaderNode.
 *   - For Light we wrap ccl::Light + companion ccl::Object in a small
 *     struct so cyc_light_set_transform can target the object.
 *   - cyc_session_t bundles ccl::Session + an internal output capture
 *     driver for post-render readback / save.
 */

#ifndef CYCLESC_INTERNAL_H
#define CYCLESC_INTERNAL_H

#ifndef CYCLES_AVAILABLE
#  error "cyclesc_internal.h must only be compiled with CYCLES_AVAILABLE=1"
#endif

#include <memory>
#include <mutex>
#include <vector>
#include <string>

#include "cyclesc.h"

#include "scene/camera.h"
#include "scene/light.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "session/session.h"
#include "session/output_driver.h"
#include "session/display_driver.h"
#include "session/buffers.h"
#include "session/tile.h"
#include "util/half.h"
#include "util/transform.h"
#include "util/types.h"

namespace cyc_internal {

/* One-time process init: util_logging_init + path_init.
 * Idempotent — safe to call from every cyc_session_create. */
void ensure_global_init();

/* Output driver that retains the final RGBA float buffer so post-render
 * code (read_framebuffer / save_image) can grab pixels after Session::wait. */
class CapturingOutputDriver : public ccl::OutputDriver {
 public:
    CapturingOutputDriver();
    ~CapturingOutputDriver() override;

    /* Final write — Cycles calls this when path tracing finishes or is
     * cancelled (PathTrace::tile_buffer_write at path_trace.cpp:886). */
    void write_render_tile(const Tile &tile) override;

    /* Progressive update — Cycles calls this on every display tick in
     * interactive mode (PathTrace::update_display at path_trace.cpp:704).
     * Without this override the default returns false → no progressive
     * IPR display. Same body as write_render_tile; returns true to
     * signal "yes I consumed the update". */
    bool update_render_tile(const Tile &tile) override;

    /* Returns true and fills out_pixels (size width*height*4) if a tile was
     * captured. Top-down RGBA order (already flipped from Cycles' bottom-up). */
    bool copy_pixels(float *out_pixels, int width, int height) const;

    int captured_width()  const { return width_; }
    int captured_height() const { return height_; }
    bool has_pixels()     const { return !pixels_.empty(); }

 private:
    mutable std::mutex mutex_;
    std::vector<float> pixels_;        /* RGBA float, top-down */
    int width_  = 0;
    int height_ = 0;
};

/* Progressive-IPR display driver: receives half4 pixels from Cycles
 * via update_begin / map_texture_buffer / update_end protocol, stores
 * them in an internal buffer for the host to read. Optional: assign a
 * GL pixel-buffer handle (Phase 2 IPR-opt 3) to enable zero-copy
 * device-direct writes via Cycles' GraphicsInterop mechanism.
 *
 * Lifecycle:
 *   - update_begin (worker) → ensure buffer sized, lock mutex
 *   - map_texture_buffer (worker) → returns buffer.data()
 *   - unmap_texture_buffer (worker) → no-op (writes already in buffer)
 *   - update_end (worker) → bump frame_version, unlock
 *   - copy_pixels (host main thread) → converts half4 → float RGBA
 *
 * frame_version is atomic so the host can poll without locking. */
class CapturingDisplayDriver : public ccl::DisplayDriver {
 public:
    CapturingDisplayDriver();
    ~CapturingDisplayDriver() override;

    /* DisplayDriver — required overrides. */
    void  next_tile_begin() override;
    bool  update_begin(const Params &params, const int w, const int h) override;
    void  update_end() override;
    ccl::half4 *map_texture_buffer() override;
    void  unmap_texture_buffer() override;
    void  zero() override;
    void  draw(const Params &params) override;

    /* Phase 2 (GL interop). The host calls this once after creating a
     * GL pixel-buffer object sized for half4 RGBA. Cycles' CUDA backend
     * picks the buffer up via graphics_interop_buffer_ and writes
     * directly into it — zero CPU↔GPU copy. */
    void set_gl_pbo(int64_t pbo_id, size_t size_bytes);
    ccl::GraphicsInteropDevice graphics_interop_get_device() override;
    void graphics_interop_activate() override;
    void graphics_interop_deactivate() override;

    /* Host hook so Cycles' worker thread can make a shared GL context
     * current before CUDA-GL queries (cuGLGetDevices) — without it,
     * CUDADevice::should_use_graphics_interop returns false on a
     * single-NVIDIA setup because the worker thread has no GL
     * context current and `cuGLGetDevices` sees zero GL-capable
     * CUDA devices. activate is also invoked from inside the
     * graphics_interop_get_device override so the first probe lands
     * with the context already current. */
    using interop_callback_fn = void (*)(void *userdata);
    void set_interop_callbacks(interop_callback_fn activate,
                               interop_callback_fn deactivate,
                               void *userdata);

    /* Host readback path (CPU fallback / Phase 1b). Copies the latest
     * captured frame as RGBA float into out (size w*h*4 floats), with
     * half→float conversion. Returns true if pixels matched (w,h) and
     * non-empty; false if no frame captured yet OR resolution changed
     * since the last update_end. */
    bool copy_pixels(float *out, int w, int h) const;

    /* Monotonic counter. Bumped on each update_end (worker thread)
     * under the buffer mutex. Host polls to detect new frames. */
    uint64_t frame_version() const { return frame_version_.load(); }

    int captured_width()  const { return width_; }
    int captured_height() const { return height_; }

 private:
    mutable std::mutex      mutex_;
    /* Double buffer: Cycles writes into buffer_write_ between
     * update_begin/update_end; on update_end we swap the two so
     * buffer_display_ always holds the most-recent COMPLETE iteration
     * the host can safely read. Without this, the host's copy_pixels
     * could snapshot Cycles' write buffer mid-iteration (e.g. CPU
     * device's TBB parallel_for on rapid orbit/zoom resets where
     * Cycles bails mid-pass) and see "black stripes from middle of
     * model" — pixels Cycles never got to in the current iteration. */
    std::vector<ccl::half4> buffer_write_;
    std::vector<ccl::half4> buffer_display_;
    int                     width_  = 0;
    int                     height_ = 0;
    std::atomic<uint64_t>   frame_version_{0};

    /* Phase 2 GL-PBO state. Stored so the driver can republish via
     * graphics_interop_buffer_.assign on Cycles' first interop query. */
    int64_t                 gl_pbo_id_     = 0;
    size_t                  gl_pbo_size_   = 0;
    bool                    interop_dirty_ = false;

    /* Host-supplied GL-context switching hooks. See set_interop_callbacks. */
    interop_callback_fn     activate_cb_     = nullptr;
    interop_callback_fn     deactivate_cb_   = nullptr;
    void                   *interop_userdata_ = nullptr;

    /* Set true when Cycles calls map_texture_buffer (the CPU/naive
     * path). Hosts that registered a GL PBO use this to detect a
     * silent interop fallback (e.g. CUDA-GL context activation didn't
     * succeed): if cpu_path_used_ flips to true, the host should
     * abandon zero-copy and read from copy_pixels instead. */
    std::atomic<bool>       cpu_path_used_{false};

 public:
    bool cpu_path_used() const { return cpu_path_used_.load(); }
};

/* Bundled (Light + Object) handle. Lights in Cycles are a Geometry node
 * that must be wrapped in an Object to receive a transform. We expose
 * a single cyc_light_t to the D side and manage both internally. */
struct LightBundle {
    ccl::Light  *light;
    ccl::Object *object;
};

/* Session wrapper. Owns a ccl::Session and the two capture drivers
 * (Output for final/write-image; Display for progressive IPR). */
struct SessionWrapper {
    std::unique_ptr<ccl::Session>     session;
    CapturingOutputDriver            *capture = nullptr;   /* borrowed from session */
    CapturingDisplayDriver           *display = nullptr;   /* borrowed from session */
    ccl::BufferParams                 buffer_params;
    int                               width  = 0;
    int                               height = 0;
    bool                              started = false;
    /* Deferred init (Pass + integrator + default-shader overrides)
     * runs once on the first cyc_session_reset call — see the
     * comment block in that function for the rationale. */
    bool                              deferred_init_done = false;
    int                               interactive = 0;
};

/* Forward/back casts. Keep all reinterpret_cast in one place. */

inline ccl::Scene  *to_scene (cyc_scene_t  *h) { return reinterpret_cast<ccl::Scene *>(h);  }
inline ccl::Mesh   *to_mesh  (cyc_mesh_t   *h) { return reinterpret_cast<ccl::Mesh *>(h);   }
inline ccl::Object *to_object(cyc_object_t *h) { return reinterpret_cast<ccl::Object *>(h); }
inline ccl::Camera *to_camera(cyc_camera_t *h) { return reinterpret_cast<ccl::Camera *>(h); }
inline ccl::Shader *to_shader(cyc_shader_t *h) { return reinterpret_cast<ccl::Shader *>(h); }
inline ccl::ShaderNode *to_shader_node(cyc_shader_node_t *h) {
    return reinterpret_cast<ccl::ShaderNode *>(h);
}
inline LightBundle  *to_light(cyc_light_t *h)  { return reinterpret_cast<LightBundle *>(h); }
inline SessionWrapper *to_session(cyc_session_t *h) {
    return reinterpret_cast<SessionWrapper *>(h);
}

inline cyc_scene_t  *from_scene (ccl::Scene  *p) { return reinterpret_cast<cyc_scene_t *>(p);  }
inline cyc_mesh_t   *from_mesh  (ccl::Mesh   *p) { return reinterpret_cast<cyc_mesh_t *>(p);   }
inline cyc_object_t *from_object(ccl::Object *p) { return reinterpret_cast<cyc_object_t *>(p); }
inline cyc_camera_t *from_camera(ccl::Camera *p) { return reinterpret_cast<cyc_camera_t *>(p); }
inline cyc_shader_t *from_shader(ccl::Shader *p) { return reinterpret_cast<cyc_shader_t *>(p); }
inline cyc_shader_node_t *from_shader_node(ccl::ShaderNode *p) {
    return reinterpret_cast<cyc_shader_node_t *>(p);
}
inline cyc_light_t *from_light(LightBundle *b) { return reinterpret_cast<cyc_light_t *>(b); }
inline cyc_session_t *from_session(SessionWrapper *w) {
    return reinterpret_cast<cyc_session_t *>(w);
}

/* Row-major 4x4 (caller-provided) → ccl::Transform.
 * Cycles' Transform stores 3 rows of 4 floats; the implicit last row is
 * (0,0,0,1). We only consume the top 3 rows. */
ccl::Transform mat4_to_transform(const float *m4x4);

}  // namespace cyc_internal

#endif /* CYCLESC_INTERNAL_H */
