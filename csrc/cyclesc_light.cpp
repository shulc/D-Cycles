/*
 * cyclesc_light.cpp — Light + companion Object.
 *
 * Cycles represents lights as Geometry that's hosted by an Object. The
 * Object carries the transform; the Light node carries type, strength,
 * size, etc. We bundle both behind a single cyc_light_t so callers
 * see one handle.
 *
 * The light's emission color comes from a Shader graph attached to the
 * Light's used_shaders. We use Scene::default_light for now, scaled by
 * the Light::strength socket (matching how cycles_xml.cpp treats lights
 * that don't specify their own shader).
 */

#include "cyclesc_internal.h"

#include "kernel/types.h"   /* LightType, PATH_RAY_* */

#include "scene/light.h"
#include "scene/object.h"
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"

#include "util/array.h"
#include "util/transform.h"
#include "util/types.h"

#include <memory>

using namespace cyc_internal;

static ccl::LightType map_light_type(cyc_light_type t)
{
    switch (t) {
        case CYC_LIGHT_POINT:      return ccl::LIGHT_POINT;
        case CYC_LIGHT_SUN:        return ccl::LIGHT_DISTANT;
        case CYC_LIGHT_SPOT:       return ccl::LIGHT_SPOT;
        case CYC_LIGHT_AREA_RECT:  return ccl::LIGHT_AREA;
        case CYC_LIGHT_AREA_DISK:  return ccl::LIGHT_AREA;
        case CYC_LIGHT_BACKGROUND: return ccl::LIGHT_BACKGROUND;
        default:                   return ccl::LIGHT_POINT;
    }
}

extern "C"
cyc_status cyc_light_create(cyc_scene_t *scene_h, cyc_light_type type, cyc_light_t **out_light)
{
    if (!scene_h || !out_light) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Scene *scene = to_scene(scene_h);
    try {
        ccl::Light *light = scene->create_node<ccl::Light>();
        light->set_light_type(map_light_type(type));
        if (type == CYC_LIGHT_AREA_DISK) light->set_ellipse(true);
        light->set_strength(ccl::make_float3(1.0f, 1.0f, 1.0f));
        light->set_is_enabled(true);
        light->set_use_mis(true);

        /* Per-light emission shader with strength=1. We can't reuse
         * `scene->default_light` here — its built-in EmissionNode
         * carries strength=0, which propagates into the Shader's
         * `emission_estimate` and trips `Light::is_lit()`'s
         * `!is_zero(emission_estimate)` guard, making Cycles skip
         * the light entirely. Symptom on macOS arm64 CPU: rays hit
         * the triangle (alpha=1) but no light reaches it, so the
         * Principled BSDF evaluates to zero RGB — the rendered PNG
         * is black. Linux x64 happened to evaluate the same code
         * path slightly differently and rendered the triangle lit;
         * either way, owning the emission shader is the correct
         * primitive. cyc_light_set_color / set_intensity continue
         * to drive Light::strength as the multiplier. */
        auto lightGraph = std::make_unique<ccl::ShaderGraph>();
        auto *emission = lightGraph->create_node<ccl::EmissionNode>();
        emission->set_color(ccl::make_float3(1.0f, 1.0f, 1.0f));
        emission->set_strength(1.0f);
        if (auto *surf_in = lightGraph->output()->input("Surface")) {
            lightGraph->connect(emission->output("Emission"), surf_in);
        }
        ccl::Shader *lightShader = scene->create_node<ccl::Shader>();
        lightShader->name = "light_emission";
        lightShader->set_graph(std::move(lightGraph));
        lightShader->tag_update(scene);
        ccl::array<ccl::Node *> shaders;
        shaders.resize(1);
        shaders[0] = static_cast<ccl::Node *>(lightShader);
        light->set_used_shaders(shaders);

        ccl::Object *obj = scene->create_node<ccl::Object>();
        obj->set_geometry(light);
        obj->set_tfm(ccl::transform_identity());
        /* Hide the light geometry from camera rays (rays from camera
         * shouldn't visually intersect the light source). */
        obj->set_visibility(ccl::PATH_RAY_ALL_VISIBILITY & ~ccl::PATH_RAY_CAMERA);

        auto *bundle = new (std::nothrow) LightBundle{light, obj};
        if (!bundle) return CYC_ERR_OUT_OF_MEMORY;
        *out_light = from_light(bundle);
        return CYC_OK;
    } catch (const std::exception &) {
        return CYC_ERR_INTERNAL;
    }
}

extern "C"
cyc_status cyc_light_destroy(cyc_scene_t *scene_h, cyc_light_t *h)
{
    if (!scene_h || !h) return CYC_ERR_INVALID_ARGUMENT;
    /* Caller MUST hold scene->mutex (the IPR pattern does). Remove the
     * Light + carrier Object from the scene first so they don't ghost
     * in IPR after the host drops them, then free our wrapper. */
    auto *bundle = to_light(h);
    ccl::Scene *scene = to_scene(scene_h);
    if (bundle->object) scene->delete_node(bundle->object);
    if (bundle->light)  scene->delete_node(bundle->light);
    delete bundle;
    return CYC_OK;
}

extern "C"
cyc_status cyc_light_set_transform(cyc_light_t *h, const float *m4x4)
{
    if (!h || !m4x4) return CYC_ERR_INVALID_ARGUMENT;
    to_light(h)->object->set_tfm(mat4_to_transform(m4x4));
    return CYC_OK;
}

extern "C"
cyc_status cyc_light_set_color(cyc_light_t *h, float r, float g, float b)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Light *L = to_light(h)->light;
    /* Preserve intensity (the max channel), tint via the rest. */
    const ccl::float3 cur = L->get_strength();
    const float intensity = ccl::max(cur.x, ccl::max(cur.y, cur.z));
    L->set_strength(ccl::make_float3(r * intensity, g * intensity, b * intensity));
    return CYC_OK;
}

extern "C"
cyc_status cyc_light_set_intensity(cyc_light_t *h, float intensity)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Light *L = to_light(h)->light;
    /* Normalize to current color hue, multiply by new intensity. */
    const ccl::float3 cur = L->get_strength();
    const float current = ccl::max(cur.x, ccl::max(cur.y, cur.z));
    if (current > 0.0f) {
        const float k = intensity / current;
        L->set_strength(ccl::make_float3(cur.x * k, cur.y * k, cur.z * k));
    } else {
        L->set_strength(ccl::make_float3(intensity, intensity, intensity));
    }
    return CYC_OK;
}

extern "C"
cyc_status cyc_light_set_size(cyc_light_t *h, float sx, float sy)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Light *L = to_light(h)->light;
    L->set_sizeu(sx);
    L->set_sizev(sy);
    L->set_size(sx);  /* point/spot uses the scalar `size` socket */
    return CYC_OK;
}

extern "C"
cyc_status cyc_light_set_spot_angle(cyc_light_t *h, float angle_rad, float blend)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    ccl::Light *L = to_light(h)->light;
    L->set_spot_angle(angle_rad);
    L->set_spot_smooth(blend);
    return CYC_OK;
}

extern "C"
cyc_status cyc_light_set_sun_angle(cyc_light_t *h, float angle_rad)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    to_light(h)->light->set_angle(angle_rad);
    return CYC_OK;
}
