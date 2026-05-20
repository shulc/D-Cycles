/*
 * cyclesc_object.cpp — Object: instance with transform and shader.
 */

#include "cyclesc_internal.h"

#include "scene/geometry.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader.h"

#include "util/array.h"
#include "util/transform.h"

using namespace cyc_internal;

extern "C"
cyc_status cyc_object_create(cyc_scene_t *scene_h, cyc_object_t **out_object)
{
    if (!scene_h || !out_object) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Scene *scene = to_scene(scene_h);
    try {
        ccl::Object *obj = scene->create_node<ccl::Object>();
        obj->set_tfm(ccl::transform_identity());
        *out_object = from_object(obj);
        return CYC_OK;
    } catch (const std::exception &) {
        return CYC_ERR_INTERNAL;
    }
}

extern "C"
cyc_status cyc_object_destroy(cyc_scene_t *scene_h, cyc_object_t *obj_h)
{
    (void)scene_h; (void)obj_h;
    return CYC_OK;  /* see cyc_mesh_destroy rationale */
}

extern "C"
cyc_status cyc_object_set_mesh(cyc_object_t *obj_h, cyc_mesh_t *mesh_h)
{
    if (!obj_h || !mesh_h) return CYC_ERR_INVALID_ARGUMENT;
    to_object(obj_h)->set_geometry(to_mesh(mesh_h));
    return CYC_OK;
}

extern "C"
cyc_status cyc_object_set_transform(cyc_object_t *obj_h, const float *m4x4)
{
    if (!obj_h || !m4x4) return CYC_ERR_INVALID_ARGUMENT;
    to_object(obj_h)->set_tfm(mat4_to_transform(m4x4));
    return CYC_OK;
}

extern "C"
cyc_status cyc_object_set_shader(cyc_object_t *obj_h, cyc_shader_t *shader_h)
{
    if (!obj_h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Object *obj = to_object(obj_h);
    ccl::Geometry *geom = obj->get_geometry();
    if (!geom) return CYC_ERR_INVALID_ARGUMENT;

    /* Shaders are bound on the geometry, not the object. Cycles stores
     * a list of "used shaders" per geometry; the per-triangle shader
     * index (set by cyc_mesh_set_triangles to 0) selects the slot. */
    ccl::array<ccl::Node *> used;
    if (shader_h) {
        used.resize(1);
        used[0] = static_cast<ccl::Node *>(to_shader(shader_h));
    }
    geom->set_used_shaders(used);
    return CYC_OK;
}

extern "C"
cyc_status cyc_object_set_visible(cyc_object_t *obj_h, int visible)
{
    if (!obj_h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Object *obj = to_object(obj_h);
    obj->set_visibility(visible ? ~0u : 0u);
    return CYC_OK;
}
