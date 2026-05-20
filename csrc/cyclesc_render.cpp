/*
 * cyclesc_render.cpp — read framebuffer / save image.
 *
 * The session's CapturingOutputDriver (installed in cyc_session_create)
 * gets pixels written to it by Cycles when the render finishes (in
 * write_render_tile). cyc_session_read_framebuffer / save_image read
 * from there.
 *
 * Image saving uses OpenImageIO directly. We apply gamma 2.2 for sRGB
 * file formats (PNG/JPG) to match the in-tree OIIOOutputDriver behavior.
 */

#include "cyclesc_internal.h"

#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace cyc_internal;

extern "C"
cyc_status cyc_session_read_framebuffer(cyc_session_t *h, float *out, int w, int height)
{
    if (!h || !out || w <= 0 || height <= 0) return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    if (!wrap->capture || !wrap->capture->has_pixels()) return CYC_ERR_INTERNAL;
    if (!wrap->capture->copy_pixels(out, w, height)) return CYC_ERR_INVALID_ARGUMENT;
    return CYC_OK;
}

static bool is_srgb_format(const std::string &filename)
{
    auto endswith = [&](const char *ext) {
        const size_t n = std::strlen(ext);
        return filename.size() >= n &&
               std::equal(filename.end() - n, filename.end(), ext,
                          [](char a, char b) { return std::tolower(a) == b; });
    };
    return endswith(".png") || endswith(".jpg") || endswith(".jpeg") ||
           endswith(".tga") || endswith(".bmp");
}

extern "C"
cyc_status cyc_session_save_image(cyc_session_t *h, const char *file_path)
{
    if (!h || !file_path) return CYC_ERR_INVALID_ARGUMENT;
    auto *wrap = to_session(h);
    if (!wrap->capture || !wrap->capture->has_pixels()) return CYC_ERR_INTERNAL;

    const int w = wrap->capture->captured_width();
    const int height = wrap->capture->captured_height();

    std::vector<float> pixels(static_cast<size_t>(w) * height * 4);
    if (!wrap->capture->copy_pixels(pixels.data(), w, height)) return CYC_ERR_INTERNAL;

    /* Apply sRGB gamma for non-linear file formats. EXR/HDR keep linear. */
    if (is_srgb_format(file_path)) {
        const float g = 1.0f / 2.2f;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = std::pow(std::max(0.0f, pixels[i + 0]), g);
            pixels[i + 1] = std::pow(std::max(0.0f, pixels[i + 1]), g);
            pixels[i + 2] = std::pow(std::max(0.0f, pixels[i + 2]), g);
            /* Alpha is linear. */
        }
    }

    std::unique_ptr<OIIO::ImageOutput> out(OIIO::ImageOutput::create(file_path));
    if (!out) return CYC_ERR_FILE_IO;

    OIIO::ImageSpec spec(w, height, 4, OIIO::TypeDesc::FLOAT);
    if (!out->open(file_path, spec)) return CYC_ERR_FILE_IO;
    if (!out->write_image(OIIO::TypeDesc::FLOAT, pixels.data())) {
        out->close();
        return CYC_ERR_FILE_IO;
    }
    out->close();
    return CYC_OK;
}
