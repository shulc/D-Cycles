/*
 * cyclesc_session.cpp — Session lifecycle, scene access, render control.
 */

#include "cyclesc_internal.h"

#include "device/device.h"
#include "scene/background.h"
#include "scene/integrator.h"
#include "scene/pass.h"
#include "scene/scene.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "session/buffers.h"
#include "session/session.h"

#include "util/log.h"
#include "util/types.h"

using namespace cyc_internal;

/* ----------------------------------------------------------------- */
/* CapturingOutputDriver — defined in header. Implementation here.   */
/* ----------------------------------------------------------------- */

CapturingOutputDriver::CapturingOutputDriver()  = default;
CapturingOutputDriver::~CapturingOutputDriver() = default;

void CapturingOutputDriver::write_render_tile(const Tile &tile)
{
    /* Mirror OIIOOutputDriver's behavior: only act on the full-frame
     * tile, ignore intermediate progressive tiles. */
    if (!(tile.size == tile.full_size)) return;

    std::lock_guard<std::mutex> lock(mutex_);
    width_  = tile.size.x;
    height_ = tile.size.y;
    pixels_.assign(static_cast<size_t>(width_) * height_ * 4, 0.0f);
    if (!tile.get_pass_pixels("combined", 4, pixels_.data())) {
        pixels_.clear();
        width_ = height_ = 0;
    }
}

bool CapturingOutputDriver::copy_pixels(float *out, int w, int h) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pixels_.empty() || w != width_ || h != height_) return false;
    /* Cycles stores tiles bottom-up. Flip on copy so callers get top-down. */
    const size_t row_bytes = static_cast<size_t>(w) * 4 * sizeof(float);
    for (int y = 0; y < h; ++y) {
        std::memcpy(out + y * w * 4,
                    pixels_.data() + (h - 1 - y) * w * 4,
                    row_bytes);
    }
    return true;
}

/* ----------------------------------------------------------------- */
/* Session API                                                         */
/* ----------------------------------------------------------------- */

static ccl::DeviceType to_ccl_device(cyc_device_type t)
{
    switch (t) {
        case CYC_DEVICE_CPU:    return ccl::DEVICE_CPU;
        case CYC_DEVICE_CUDA:   return ccl::DEVICE_CUDA;
        case CYC_DEVICE_OPTIX:  return ccl::DEVICE_OPTIX;
        case CYC_DEVICE_HIP:    return ccl::DEVICE_HIP;
        case CYC_DEVICE_METAL:  return ccl::DEVICE_METAL;
        case CYC_DEVICE_ONEAPI: return ccl::DEVICE_ONEAPI;
        default:                return ccl::DEVICE_CPU;
    }
}

extern "C"
cyc_status cyc_session_create(const cyc_session_params *params, cyc_session_t **out_session)
{
    if (!params || !out_session) return CYC_ERR_INVALID_ARGUMENT;

    ensure_global_init();

    /* Pick a matching device. */
    const ccl::DeviceType wanted = to_ccl_device(params->device_type);
    ccl::vector<ccl::DeviceInfo> devices =
        ccl::Device::available_devices(
            static_cast<ccl::DeviceTypeMask>(1 << wanted));
    if (devices.empty()) return CYC_ERR_NO_SUITABLE_DEVICE;

    int idx = params->device_index;
    if (idx < 0 || idx >= static_cast<int>(devices.size())) idx = 0;

    ccl::SessionParams sp;
    sp.device      = devices[idx];
    sp.headless    = true;
    sp.background  = (params->interactive == 0);
    sp.samples     = params->samples > 0 ? params->samples : 64;
    sp.threads     = params->threads;
    sp.use_auto_tile = false;
    sp.tile_size   = 0;
    sp.shadingsystem = ccl::SHADINGSYSTEM_SVM;

    ccl::SceneParams scene_params;
    scene_params.shadingsystem = ccl::SHADINGSYSTEM_SVM;
    scene_params.background    = sp.background;

    auto *wrap = new (std::nothrow) SessionWrapper();
    if (!wrap) return CYC_ERR_OUT_OF_MEMORY;
    try {
        wrap->session = std::make_unique<ccl::Session>(sp, scene_params);
    } catch (const std::exception &) {
        delete wrap;
        return CYC_ERR_INTERNAL;
    }

    /* Install our capturing output driver. Cycles takes ownership; we
     * remember a borrowed pointer so we can read pixels post-render. */
    auto capture = std::make_unique<CapturingOutputDriver>();
    wrap->capture = capture.get();
    wrap->session->set_output_driver(std::move(capture));

    /* Default pass: a single COMBINED pass. Required for image output. */
    ccl::Scene *scene = wrap->session->scene.get();
    ccl::Pass *pass = scene->create_node<ccl::Pass>();
    pass->set_name(ccl::ustring("combined"));
    pass->set_type(ccl::PASS_COMBINED);

    /* Drop sample count on the integrator too — Cycles checks both. */
    if (scene->integrator) {
        scene->integrator->set_min_bounce(0);
        scene->integrator->set_max_bounce(8);
    }

    /* Two scene-default tweaks. Both are non-destructive defaults the
     * caller can override per-shader later. */

    /* 1. Background — Cycles' default_background graph is empty. Give
     *    it a faint neutral sky so a smoke test renders something
     *    visible even without an explicit env light. */
    {
        auto bg_graph = std::make_unique<ccl::ShaderGraph>();
        auto *bg = bg_graph->create_node<ccl::BackgroundNode>();
        bg->set_color(ccl::make_float3(0.05f, 0.05f, 0.05f));
        bg->set_strength(1.0f);
        bg_graph->connect(bg->output("Background"),
                          bg_graph->output()->input("Surface"));
        scene->default_background->set_graph(std::move(bg_graph));
        scene->default_background->tag_update(scene);
    }

    /* 2. Default light shader — Cycles initializes it with emission
     *    strength=0, so the per-Light strength socket multiplies
     *    against zero and the light contributes nothing. Bump the
     *    shader's emission to unit strength so Light::strength behaves
     *    as a power knob the caller expects. */
    {
        auto light_graph = std::make_unique<ccl::ShaderGraph>();
        auto *em = light_graph->create_node<ccl::EmissionNode>();
        em->set_color(ccl::make_float3(1.0f, 1.0f, 1.0f));
        em->set_strength(1.0f);
        light_graph->connect(em->output("Emission"),
                             light_graph->output()->input("Surface"));
        scene->default_light->set_graph(std::move(light_graph));
        scene->default_light->tag_update(scene);
    }

    *out_session = from_session(wrap);
    return CYC_OK;
}

extern "C"
void cyc_session_destroy(cyc_session_t *h)
{
    if (!h) return;
    auto *wrap = to_session(h);
    /* Session destructor will join its worker thread. */
    wrap->session.reset();
    delete wrap;
}

extern "C"
cyc_scene_t *cyc_session_scene(cyc_session_t *h)
{
    if (!h) return nullptr;
    auto *wrap = to_session(h);
    return from_scene(wrap->session->scene.get());
}

extern "C"
cyc_status cyc_session_reset(cyc_session_t *h, int w, int height)
{
    if (!h || w <= 0 || height <= 0) return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    wrap->width  = w;
    wrap->height = height;
    wrap->buffer_params = ccl::BufferParams();
    wrap->buffer_params.width       = w;
    wrap->buffer_params.height      = height;
    wrap->buffer_params.full_width  = w;
    wrap->buffer_params.full_height = height;

    /* Camera resolution must match buffer params. */
    ccl::Scene *scene = wrap->session->scene.get();
    if (scene->camera) {
        scene->camera->set_full_width(w);
        scene->camera->set_full_height(height);
        scene->camera->compute_auto_viewplane();
        scene->camera->need_flags_update = true;
        scene->camera->need_device_update = true;
        scene->camera->update(scene);
    }

    wrap->session->reset(wrap->session->params, wrap->buffer_params);
    return CYC_OK;
}

extern "C"
cyc_status cyc_session_start(cyc_session_t *h)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    if (wrap->width == 0 || wrap->height == 0) {
        /* User forgot cyc_session_reset; default to 512x512. */
        const cyc_status s = cyc_session_reset(h, 512, 512);
        if (s != CYC_OK) return s;
    }
    try {
        wrap->session->start();
        wrap->started = true;
    } catch (const std::exception &) {
        return CYC_ERR_INTERNAL;
    }
    return CYC_OK;
}

extern "C"
cyc_status cyc_session_cancel(cyc_session_t *h)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    to_session(h)->session->cancel();
    return CYC_OK;
}

extern "C"
cyc_status cyc_session_wait(cyc_session_t *h)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    to_session(h)->session->wait();
    return CYC_OK;
}

extern "C"
cyc_status cyc_session_progress(cyc_session_t *h, float *out_progress)
{
    if (!h || !out_progress) return CYC_ERR_INVALID_ARGUMENT;
    /* Session::get_progress is declared in session.h but not defined in
     * the static lib. Use the Progress member directly — same value. */
    *out_progress = static_cast<float>(
        to_session(h)->session->progress.get_progress());
    return CYC_OK;
}
