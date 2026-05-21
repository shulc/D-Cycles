/*
 * cyclesc_session.cpp — Session lifecycle, scene access, render control.
 */

#include "cyclesc_internal.h"

#include <cstdio>
#include <cstdlib>
#include <atomic>

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

/* Snapshot the "combined" pass of a full-frame tile into pixels_.
 * Used by both write_render_tile (final) and update_render_tile
 * (progressive). Returns true on success. */
static bool capture_tile_combined(const ccl::OutputDriver::Tile &tile,
                                  std::mutex &mutex,
                                  std::vector<float> &pixels,
                                  int &width,
                                  int &height,
                                  const char *origin)
{
    static std::atomic<int> trace_on{-1};
    if (trace_on.load() == -1) {
        trace_on.store(std::getenv("CYC_TILE_TRACE") != nullptr ? 1 : 0);
    }
    if (trace_on.load() == 1) {
        std::fprintf(stderr,
            "[cyc:tile] %s size=%dx%d full=%dx%d offset=%d,%d layer='%s'\n",
            origin, tile.size.x, tile.size.y,
            tile.full_size.x, tile.full_size.y,
            tile.offset.x, tile.offset.y,
            std::string(tile.layer).c_str());
        std::fflush(stderr);
    }

    /* Mirror OIIOOutputDriver's behavior: only act on the full-frame
     * tile, ignore intermediate sub-tiles. */
    if (!(tile.size == tile.full_size)) return false;

    std::lock_guard<std::mutex> lock(mutex);
    width  = tile.size.x;
    height = tile.size.y;
    pixels.assign(static_cast<size_t>(width) * height * 4, 0.0f);
    if (!tile.get_pass_pixels("combined", 4, pixels.data())) {
        if (trace_on.load() == 1) {
            std::fprintf(stderr, "[cyc:tile] %s get_pass_pixels('combined') FAILED\n", origin);
            std::fflush(stderr);
        }
        pixels.clear();
        width = height = 0;
        return false;
    }
    if (trace_on.load() == 1) {
        /* Fingerprint a CENTER STRIP of the image — that's where rendered
         * geometry usually lives. First 4 KB hashes ~top 3 rows which are
         * just background gray and stay identical regardless of camera. */
        const size_t total_pix = static_cast<size_t>(width) * height;
        const size_t center_start = (total_pix / 2 - 512) * 4;   /* 1024 RGBA pixels around center */
        const size_t center_end   = std::min(center_start + 4096 / sizeof(float) * 4 * sizeof(float) / sizeof(float),
                                              pixels.size());
        unsigned long long h = 1469598103934665603ULL;
        for (size_t i = center_start; i < center_end; ++i) {
            const unsigned int u = *reinterpret_cast<const unsigned int *>(&pixels[i]);
            h ^= (u & 0xff);        h *= 1099511628211ULL;
            h ^= ((u >> 8) & 0xff); h *= 1099511628211ULL;
            h ^= ((u >> 16) & 0xff); h *= 1099511628211ULL;
            h ^= ((u >> 24) & 0xff); h *= 1099511628211ULL;
        }
        const size_t center_px = (total_pix / 2) * 4;
        std::fprintf(stderr,
            "[cyc:tile] %s captured %d bytes  hash=%016llx  center=(%.3f,%.3f,%.3f,%.3f)\n",
            origin, width * height * 16, h,
            pixels[center_px], pixels[center_px+1],
            pixels[center_px+2], pixels[center_px+3]);
        std::fflush(stderr);
    }
    return true;
}

void CapturingOutputDriver::write_render_tile(const Tile &tile)
{
    capture_tile_combined(tile, mutex_, pixels_, width_, height_, "write");
}

bool CapturingOutputDriver::update_render_tile(const Tile &tile)
{
    return capture_tile_combined(tile, mutex_, pixels_, width_, height_, "update");
}

/* ----------------------------------------------------------------- */
/* CapturingDisplayDriver — progressive IPR via DisplayDriver protocol.
 *
 * Cycles' interactive loop calls update_begin → map_texture_buffer →
 * (fill) → unmap_texture_buffer → update_end on every progressive
 * iteration.  We retain the half4 buffer for the host to read via
 * copy_pixels() (CPU path) or via a GL PBO assigned by set_gl_pbo()
 * (interop path).                                                    */
/* ----------------------------------------------------------------- */

CapturingDisplayDriver::CapturingDisplayDriver()  = default;
CapturingDisplayDriver::~CapturingDisplayDriver() = default;

void CapturingDisplayDriver::next_tile_begin()
{
    /* Informational — Cycles signals start of a new tile.  No-op:
     * we accumulate into one buffer and the host reads the latest. */
}

bool CapturingDisplayDriver::update_begin(const Params &params, const int w, const int h)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (w <= 0 || h <= 0) return false;

    static std::atomic<int> trace_on{-1};
    if (trace_on.load() == -1)
        trace_on.store(std::getenv("CYC_TILE_TRACE") != nullptr ? 1 : 0);
    if (trace_on.load() == 1) {
        std::fprintf(stderr,
            "[cyc:display] update_begin  tex=%dx%d  params.size=%dx%d full=%dx%d off=%d,%d\n",
            w, h, params.size.x, params.size.y,
            params.full_size.x, params.full_size.y,
            params.full_offset.x, params.full_offset.y);
        std::fflush(stderr);
    }

    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (buffer_.size() != need || width_ != w || height_ != h) {
        buffer_.assign(need, ccl::half4{ccl::half(0), ccl::half(0), ccl::half(0), ccl::half(0)});
        width_  = w;
        height_ = h;
    }

    /* Phase 2: if the host has registered a GL PBO since last call,
     * republish it through graphics_interop_buffer_ so Cycles' device
     * backend picks it up on the next iteration. */
    if (interop_dirty_) {
        graphics_interop_buffer_.assign(
            ccl::GraphicsInteropDevice::OPENGL, gl_pbo_id_, gl_pbo_size_);
        interop_dirty_ = false;
    }
    return true;
}

void CapturingDisplayDriver::update_end()
{
    /* Increment the visible-frame counter so the host can poll it
     * without locking.  Memory order: release pairs with host's
     * acquire on frame_version_.load(). */
    frame_version_.fetch_add(1, std::memory_order_release);
}

ccl::half4 *CapturingDisplayDriver::map_texture_buffer()
{
    /* Host-visible flag: this is the CPU/naive path — host using zero-
     * copy interop must downgrade to copy_pixels readback. */
    cpu_path_used_.store(true, std::memory_order_release);

    static std::atomic<int> trace_on{-1};
    if (trace_on.load() == -1)
        trace_on.store(std::getenv("CYC_TILE_TRACE") != nullptr ? 1 : 0);
    if (trace_on.load() == 1) {
        std::fprintf(stderr, "[cyc:display] map_texture_buffer (CPU path)\n");
        std::fflush(stderr);
    }
    /* Note: this is invoked by Cycles inside the update_begin/_end
     * window — the mutex is NOT re-acquired here because Cycles
     * single-threads the map/unmap pair per device update.  If we
     * wanted multi-device support we'd need a different scheme. */
    if (buffer_.empty()) return nullptr;
    return buffer_.data();
}

void CapturingDisplayDriver::unmap_texture_buffer()
{
    /* No-op — Cycles wrote directly into buffer_. */
}

void CapturingDisplayDriver::zero()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &p : buffer_) {
        p.x = ccl::half(0); p.y = ccl::half(0);
        p.z = ccl::half(0); p.w = ccl::half(0);
    }
}

void CapturingDisplayDriver::draw(const Params & /*params*/)
{
    /* Host doesn't call ccl::Session::draw() — we read pixels via
     * copy_pixels() instead.  No-op here. */
}

void CapturingDisplayDriver::set_gl_pbo(int64_t pbo_id, size_t size_bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    gl_pbo_id_     = pbo_id;
    gl_pbo_size_   = size_bytes;
    interop_dirty_ = true;
}

ccl::GraphicsInteropDevice CapturingDisplayDriver::graphics_interop_get_device()
{
    static std::atomic<int> trace_on{-1};
    if (trace_on.load() == -1)
        trace_on.store(std::getenv("CYC_TILE_TRACE") != nullptr ? 1 : 0);

    /* Cycles' CUDA backend will call cuGLGetDevices right after this
     * returns. That query needs a GL context to be current on the
     * calling thread (this worker thread!). Hook the host so it can
     * SDL_GL_MakeCurrent a shared context here. */
    if (activate_cb_) activate_cb_(interop_userdata_);

    /* NB: Cycles invokes this from inside the update_begin/_end window
     * of its first iteration (via should_use_graphics_interop). Our
     * update_lock_ still owns mutex_ at this point — re-acquiring
     * would deadlock the worker against itself. The interop fields
     * (gl_pbo_id_, gl_pbo_size_) are set by the host before the worker
     * starts (cyc_session_display_bind_gl_pbo called pre-start in
     * bootLiveSession); plain reads are safe enough. */
    ccl::GraphicsInteropDevice dev;
    if (gl_pbo_id_ != 0) {
        dev.type = ccl::GraphicsInteropDevice::OPENGL;
    }
    if (trace_on.load() == 1) {
        std::fprintf(stderr,
            "[cyc:display] graphics_interop_get_device → type=%d  pbo=%lld\n",
            (int)dev.type, (long long)gl_pbo_id_);
        std::fflush(stderr);
    }
    return dev;
}

void CapturingDisplayDriver::graphics_interop_activate()
{
    if (activate_cb_) activate_cb_(interop_userdata_);
}

void CapturingDisplayDriver::graphics_interop_deactivate()
{
    if (deactivate_cb_) deactivate_cb_(interop_userdata_);
}

void CapturingDisplayDriver::set_interop_callbacks(
    interop_callback_fn activate,
    interop_callback_fn deactivate,
    void *userdata)
{
    std::lock_guard<std::mutex> lock(mutex_);
    activate_cb_      = activate;
    deactivate_cb_    = deactivate;
    interop_userdata_ = userdata;
}

bool CapturingDisplayDriver::copy_pixels(float *out, int w, int h) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (buffer_.empty() || w != width_ || h != height_) return false;

    /* Cycles writes pixels bottom-up; flip to top-down on copy so the
     * host's glTexImage2D upload matches ImGui's expected orientation
     * without a UV flip in the panel. Matches CapturingOutputDriver. */
    for (int y = 0; y < h; ++y) {
        const ccl::half4 *src = &buffer_[(h - 1 - y) * w];
        float            *dst = &out[y * w * 4];
        for (int x = 0; x < w; ++x) {
            const ccl::float4 f = ccl::half4_to_float4_image(src[x]);
            dst[x * 4 + 0] = f.x;
            dst[x * 4 + 1] = f.y;
            dst[x * 4 + 2] = f.z;
            dst[x * 4 + 3] = f.w;
        }
    }

    static std::atomic<int> trace_on{-1};
    if (trace_on.load() == -1)
        trace_on.store(std::getenv("CYC_TILE_TRACE") != nullptr ? 1 : 0);
    if (trace_on.load() == 1) {
        const size_t n = static_cast<size_t>(w) * h;
        float mn = out[0], mx = out[0];
        size_t mxIdx = 0;
        for (size_t i = 0; i < n; ++i) {
            const float v = out[i * 4];   /* red channel */
            if (v < mn) mn = v;
            if (v > mx) { mx = v; mxIdx = i; }
        }
        const size_t mid = (n / 2) * 4;
        std::fprintf(stderr,
            "[cyc:display] copy_pixels  center=(%.3f,%.3f,%.3f) "
            "min=%.3f max=%.3f@%zu  fv=%lu\n",
            out[mid], out[mid+1], out[mid+2],
            mn, mx, mxIdx,
            (unsigned long)frame_version_.load());
        std::fflush(stderr);
    }
    return true;
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
    /* headless=true forces RenderScheduler::work_need_update_display
     * to return false (render_scheduler.cpp:1027), which means
     * PathTrace::update_display never invokes OutputDriver::update_render_tile.
     * For interactive IPR we need progressive tiles to flow into the
     * driver — leave headless OFF when interactive is requested. */
    sp.headless    = (params->interactive == 0);
    sp.background  = (params->interactive == 0);
    /* Progressive resolution divider starts rendering at w/8 x h/8 and
     * walks up to full res. Writes a small dense tile at the top of the
     * display_rgba_half_ device buffer; after copy_from_device + the
     * driver's copy_pixels_to_texture stride remapping, *most* of the
     * output texture stays at its initial-zero state (visible as a
     * mostly-black image with a tiny rendered patch in the corner).
     * Until the host implements proper resolution-divider awareness
     * (zoom-to-full-res over multiple frames), force divider OFF so
     * Cycles renders at full resolution from sample 0. */
    sp.use_resolution_divider = false;
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

    /* Install both capture drivers. Cycles takes ownership; we
     * remember borrowed pointers so we can read pixels (for
     * Output: post-render final; for Display: progressive IPR). */
    auto capture = std::make_unique<CapturingOutputDriver>();
    wrap->capture = capture.get();
    wrap->session->set_output_driver(std::move(capture));

    auto display = std::make_unique<CapturingDisplayDriver>();
    wrap->display = display.get();
    wrap->session->set_display_driver(std::move(display));

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
    wrap->buffer_params.width         = w;
    wrap->buffer_params.height        = height;
    wrap->buffer_params.full_width    = w;
    wrap->buffer_params.full_height   = height;
    /* window_* defaults to 0; copy_to_display_naive uses
     * effective_buffer_params_.window_width/height to compute the
     * region copied to the display driver. Leaving those at zero can
     * make Cycles' kernel render at full size but the display copy
     * only push a 0×0 slice — texture stays mostly at its init-zero
     * state (visible as a black cube on the initial 5% background).
     * Explicitly mirror full_*. */
    wrap->buffer_params.window_x      = 0;
    wrap->buffer_params.window_y      = 0;
    wrap->buffer_params.window_width  = w;
    wrap->buffer_params.window_height = height;

    /* Camera resolution. set_full_width/height are socket setters and
     * auto-tag dirty bits; the worker's update_scene picks them up under
     * scene->mutex on its next iteration. Do NOT call scene->camera->
     * update(scene) here — it would (1) run device_update from the main
     * thread without holding scene->mutex (race with the worker), and
     * (2) clear the dirty bits set by the caller's scene-mutex'd
     * sync_blueprint, masking the camera matrix update so the device
     * keeps rendering with the old camera.  Just push the sockets and
     * let the worker's update_scene do the device upload. */
    ccl::Scene *scene = wrap->session->scene.get();
    if (scene->camera) {
        scene->camera->set_full_width(w);
        scene->camera->set_full_height(height);
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

/* ---- Interactive-mode scene sync API (Blender's IPR pattern) -------- */

/* Non-blocking attempt to acquire the scene mutex. Returns 1 if the
 * caller now owns the mutex (must pair with cyc_session_scene_unlock),
 * 0 if the worker thread is currently holding it.
 *
 * Entry point for the "mutex + reset + start" IPR pattern that
 * avoids destroying and recreating the session on every scene change. */
extern "C"
int cyc_session_scene_try_lock(cyc_session_t *h)
{
    if (!h) return 0;
    auto *wrap = to_session(h);
    if (!wrap->session || !wrap->session->scene) return 0;
    return wrap->session->scene->mutex.try_lock() ? 1 : 0;
}

extern "C"
void cyc_session_scene_unlock(cyc_session_t *h)
{
    if (!h) return;
    auto *wrap = to_session(h);
    if (wrap->session && wrap->session->scene) {
        wrap->session->scene->mutex.unlock();
    }
}

extern "C"
void cyc_session_set_pause(cyc_session_t *h, int paused)
{
    if (!h) return;
    to_session(h)->session->set_pause(paused != 0);
}

extern "C"
void cyc_session_set_samples(cyc_session_t *h, int samples)
{
    if (!h || samples <= 0) return;
    to_session(h)->session->set_samples(samples);
}

extern "C"
int cyc_session_ready_to_reset(cyc_session_t *h)
{
    if (!h) return 0;
    auto *wrap = to_session(h);
    if (!wrap->session) return 0;
    return wrap->session->ready_to_reset() ? 1 : 0;
}

/* ---- Display driver (progressive IPR) -------------------------------- */

extern "C"
cyc_status cyc_session_display_read_pixels(cyc_session_t *h,
                                           float *out_rgba, int w, int height)
{
    if (!h || !out_rgba || w <= 0 || height <= 0)
        return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    if (!wrap->display) return CYC_ERR_INTERNAL;
    if (!wrap->display->copy_pixels(out_rgba, w, height))
        return CYC_ERR_INTERNAL;   /* no frame yet, or size mismatch */
    return CYC_OK;
}

extern "C"
unsigned long long cyc_session_display_version(cyc_session_t *h)
{
    if (!h) return 0;
    auto *wrap = to_session(h);
    if (!wrap->display) return 0;
    return wrap->display->frame_version();
}

extern "C"
cyc_status cyc_session_display_bind_gl_pbo(cyc_session_t *h,
                                           unsigned long long gl_pbo_id,
                                           unsigned long long size_bytes)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    if (!wrap->display) return CYC_ERR_INTERNAL;
    wrap->display->set_gl_pbo(static_cast<int64_t>(gl_pbo_id),
                              static_cast<size_t>(size_bytes));
    return CYC_OK;
}

/* True after Cycles' worker has fallen back to the CPU naive path
 * (map_texture_buffer) at least once since this session started.
 * Hosts that opted in via cyc_session_display_bind_gl_pbo poll this
 * after the first update_end — if true, zero-copy didn't take and
 * they should read pixels via cyc_session_display_read_pixels instead. */
extern "C"
int cyc_session_display_cpu_path_used(cyc_session_t *h)
{
    if (!h) return 0;
    auto *wrap = to_session(h);
    if (!wrap->display) return 0;
    return wrap->display->cpu_path_used() ? 1 : 0;
}

/* Register GL-context switching callbacks. Cycles' interop queries
 * (cuGLGetDevices, cuGraphicsGLRegisterBuffer) need a GL context
 * current on the calling worker thread. Host typically creates a
 * shared SDL_GLContext and uses these callbacks to SDL_GL_MakeCurrent
 * it on whichever thread Cycles invokes the driver from. */
extern "C"
cyc_status cyc_session_display_set_interop_callbacks(
    cyc_session_t *h,
    void (*activate)(void *userdata),
    void (*deactivate)(void *userdata),
    void *userdata)
{
    if (!h) return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    if (!wrap->display) return CYC_ERR_INTERNAL;
    wrap->display->set_interop_callbacks(activate, deactivate, userdata);
    return CYC_OK;
}
