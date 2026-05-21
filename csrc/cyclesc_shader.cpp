/*
 * cyclesc_shader.cpp — Shader (graph-backed material).
 *
 * Phase 0c only exposes cyc_shader_create_principled() + setters for
 * the Principled BSDF base color/roughness/metallic/IOR/emission/
 * transmission. The full graph API (add_node/connect) is deferred to
 * Phase 2 where vibe3d's Shader Tree compiler needs it.
 */

#include "cyclesc_internal.h"

#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"

#include "util/types.h"
#include "util/unique_ptr.h"

#include <memory>

using namespace cyc_internal;

/* Find the Principled BSDF node attached to a Shader's graph (we put
 * exactly one when cyc_shader_create_principled was used). Returns
 * nullptr if no Principled BSDF is present. */
static ccl::PrincipledBsdfNode *find_principled(ccl::Shader *shader)
{
    if (!shader || !shader->graph) return nullptr;
    for (ccl::ShaderNode *node : shader->graph->nodes) {
        if (auto *p = dynamic_cast<ccl::PrincipledBsdfNode *>(node)) {
            return p;
        }
    }
    return nullptr;
}

extern "C"
cyc_status cyc_shader_create(cyc_scene_t *scene_h, cyc_shader_t **out)
{
    if (!scene_h || !out) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Scene *scene = to_scene(scene_h);
    ccl::Shader *shader = scene->create_node<ccl::Shader>();
    /* Empty graph — caller is expected to populate via the graph API
     * (Phase 2). Without nodes, the shader contributes nothing. */
    shader->set_graph(std::make_unique<ccl::ShaderGraph>());
    *out = from_shader(shader);
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_destroy(cyc_scene_t *scene_h, cyc_shader_t *h)
{
    if (!scene_h || !h) return CYC_ERR_INVALID_ARGUMENT;
    /* Caller MUST hold scene->mutex (IPR pattern). */
    to_scene(scene_h)->delete_node(to_shader(h));
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_create_principled(cyc_scene_t *scene_h, cyc_shader_t **out)
{
    if (!scene_h || !out) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Scene *scene = to_scene(scene_h);
    try {
        ccl::Shader *shader = scene->create_node<ccl::Shader>();

        auto graph = std::make_unique<ccl::ShaderGraph>();
        auto *bsdf = graph->create_node<ccl::PrincipledBsdfNode>();
        /* Set defaults that look like a generic diffuse-ish material so
         * the smoke test doesn't render a mirror by accident. */
        bsdf->set_base_color(ccl::make_float3(0.8f, 0.8f, 0.8f));
        bsdf->set_roughness(0.5f);
        bsdf->set_metallic(0.0f);

        /* Connect bsdf.BSDF → output.Surface. */
        ccl::ShaderInput  *surf_in   = graph->output()->input("Surface");
        ccl::ShaderOutput *bsdf_out  = bsdf->output("BSDF");
        if (surf_in && bsdf_out) graph->connect(bsdf_out, surf_in);

        shader->set_graph(std::move(graph));
        shader->tag_update(scene);

        *out = from_shader(shader);
        return CYC_OK;
    } catch (const std::exception &) {
        return CYC_ERR_INTERNAL;
    }
}

extern "C"
cyc_status cyc_shader_set_principled_base_color(cyc_shader_t *h, float r, float g, float b)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::PrincipledBsdfNode *p = find_principled(to_shader(h));
    if (!p) return CYC_ERR_INVALID_ARGUMENT;
    p->set_base_color(ccl::make_float3(r, g, b));
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_set_principled_roughness(cyc_shader_t *h, float r)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::PrincipledBsdfNode *p = find_principled(to_shader(h));
    if (!p) return CYC_ERR_INVALID_ARGUMENT;
    p->set_roughness(r);
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_set_principled_metallic(cyc_shader_t *h, float m)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::PrincipledBsdfNode *p = find_principled(to_shader(h));
    if (!p) return CYC_ERR_INVALID_ARGUMENT;
    p->set_metallic(m);
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_set_principled_emission(cyc_shader_t *h, float r, float g, float b, float strength)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::PrincipledBsdfNode *p = find_principled(to_shader(h));
    if (!p) return CYC_ERR_INVALID_ARGUMENT;
    p->set_emission_color(ccl::make_float3(r, g, b));
    p->set_emission_strength(strength);
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_set_principled_specular(cyc_shader_t *h, float ior)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::PrincipledBsdfNode *p = find_principled(to_shader(h));
    if (!p) return CYC_ERR_INVALID_ARGUMENT;
    p->set_ior(ior);
    return CYC_OK;
}

extern "C"
cyc_status cyc_shader_set_principled_transmission(cyc_shader_t *h, float t)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::PrincipledBsdfNode *p = find_principled(to_shader(h));
    if (!p) return CYC_ERR_INVALID_ARGUMENT;
    p->set_transmission_weight(t);
    return CYC_OK;
}

/* Generic graph API — Phase 2 work. */

extern "C"
cyc_status cyc_shader_add_node(cyc_shader_t *, cyc_node_type, cyc_shader_node_t **)
{ return CYC_ERR_UNSUPPORTED; }

extern "C"
cyc_status cyc_shader_connect(cyc_shader_t *, cyc_shader_node_t *, const char *,
                              cyc_shader_node_t *, const char *)
{ return CYC_ERR_UNSUPPORTED; }

extern "C"
cyc_status cyc_shader_node_set_input_float(cyc_shader_node_t *, const char *, float)
{ return CYC_ERR_UNSUPPORTED; }

extern "C"
cyc_status cyc_shader_node_set_input_color(cyc_shader_node_t *, const char *, float, float, float)
{ return CYC_ERR_UNSUPPORTED; }

extern "C"
cyc_status cyc_shader_node_set_input_vector(cyc_shader_node_t *, const char *, float, float, float)
{ return CYC_ERR_UNSUPPORTED; }
