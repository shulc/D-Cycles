/*
 * cyclesc_mesh.cpp — Mesh construction.
 */

#include "cyclesc_internal.h"

#include "scene/mesh.h"
#include "scene/scene.h"

#include "util/array.h"
#include "util/types.h"

using namespace cyc_internal;

extern "C"
cyc_status cyc_mesh_create(cyc_scene_t *scene_h, cyc_mesh_t **out_mesh)
{
    if (!scene_h || !out_mesh) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Scene *scene = to_scene(scene_h);
    try {
        ccl::Mesh *mesh = scene->create_node<ccl::Mesh>();
        *out_mesh = from_mesh(mesh);
        return CYC_OK;
    } catch (const std::exception &) {
        return CYC_ERR_INTERNAL;
    }
}

extern "C"
cyc_status cyc_mesh_destroy(cyc_scene_t *scene_h, cyc_mesh_t *mesh_h)
{
    if (!scene_h || !mesh_h) return CYC_ERR_INVALID_ARGUMENT;
    /* Caller MUST hold scene->mutex (the IPR pattern does — see
     * cycles_backend.resetAccumulation). delete_node enqueues into the
     * scene's deletion list; the worker's next update_scene pass clears
     * the device-side allocation. */
    to_scene(scene_h)->delete_node(to_mesh(mesh_h));
    return CYC_OK;
}

extern "C"
cyc_status cyc_mesh_set_verts(cyc_mesh_t *h, const float *verts, int num_verts)
{
    if (!h || !verts || num_verts <= 0) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Mesh *mesh = to_mesh(h);

    ccl::array<ccl::float3> P;
    P.resize(num_verts);
    for (int i = 0; i < num_verts; ++i) {
        P[i] = ccl::make_float3(verts[i * 3 + 0],
                                verts[i * 3 + 1],
                                verts[i * 3 + 2]);
    }
    mesh->set_verts(P);
    return CYC_OK;
}

extern "C"
cyc_status cyc_mesh_set_triangles(cyc_mesh_t *h,
                                  const int *tri_indices, int num_tris,
                                  const int *smooth_flags)
{
    if (!h || !tri_indices || num_tris <= 0) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Mesh *mesh = to_mesh(h);

    /* reserve_mesh sets capacity (not size). add_triangle appends. We
     * use it because it auto-resizes the per-triangle shader and smooth
     * arrays alongside, which set_triangles alone wouldn't. */
    mesh->reserve_mesh(mesh->get_verts().size(), num_tris);
    for (int i = 0; i < num_tris; ++i) {
        const int v0 = tri_indices[i * 3 + 0];
        const int v1 = tri_indices[i * 3 + 1];
        const int v2 = tri_indices[i * 3 + 2];
        const bool smooth = smooth_flags ? (smooth_flags[i] != 0) : false;
        /* Shader index 0 = first slot of mesh->used_shaders. Whoever
         * assigns the shader (cyc_object_set_shader) populates that. */
        mesh->add_triangle(v0, v1, v2, /*shader=*/0, smooth);
    }
    mesh->tag_verts_modified();
    return CYC_OK;
}

extern "C"
cyc_status cyc_mesh_set_normals(cyc_mesh_t *h, const float *normals, int num_normals)
{
    /* Per-vertex normals via ATTR_STD_VERTEX_NORMAL. Phase 0c-5 smoke
     * test renders one triangle without explicit normals (Cycles
     * computes face normals automatically), so we accept this but
     * don't yet wire it through. */
    if (!h || !normals || num_normals <= 0) return CYC_ERR_INVALID_ARGUMENT;
    return CYC_ERR_UNSUPPORTED;
}

extern "C"
cyc_status cyc_mesh_set_uvs(cyc_mesh_t *h, const float *uvs, int num_uvs,
                            const char *uv_layer_name)
{
    if (!h || !uvs || num_uvs <= 0) return CYC_ERR_INVALID_ARGUMENT;
    (void)uv_layer_name;
    return CYC_ERR_UNSUPPORTED;
}
