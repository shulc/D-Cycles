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
#include "session/buffers.h"
#include "session/tile.h"
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

    void write_render_tile(const Tile &tile) override;

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

/* Bundled (Light + Object) handle. Lights in Cycles are a Geometry node
 * that must be wrapped in an Object to receive a transform. We expose
 * a single cyc_light_t to the D side and manage both internally. */
struct LightBundle {
    ccl::Light  *light;
    ccl::Object *object;
};

/* Session wrapper. Owns a ccl::Session and an output capture driver. */
struct SessionWrapper {
    std::unique_ptr<ccl::Session>     session;
    CapturingOutputDriver            *capture = nullptr;   /* borrowed from session */
    ccl::BufferParams                 buffer_params;
    int                               width  = 0;
    int                               height = 0;
    bool                              started = false;
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
