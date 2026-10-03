// SPDX-License-Identifier: GPL-3.0-or-later
#include "MjpegDecoder.h"

#include <turbojpeg.h>

namespace cam {

namespace {

// Walks the marker segments after SOI and reports whether a frame header
// (SOFn) comes before the scan data. libjpeg-turbo 3.x keeps the previous
// image's header when handed a frame without one, so such frames must be
// rejected here rather than by the decoder.
bool hasFrameHeader(const uint8_t *data, size_t size)
{
    size_t i = 2;
    while (i + 1 < size) {
        if (data[i] != 0xFF)
            return false;
        while (i + 1 < size && data[i + 1] == 0xFF) // fill bytes
            ++i;
        if (i + 1 >= size)
            return false;
        const uint8_t m = data[i + 1];
        i += 2;
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) // TEM, RSTn: no length
            continue;
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
            return true;
        if (m == 0xDA || m == 0xD9 || m == 0x00) // scan or end before any frame header
            return false;
        if (i + 1 >= size)
            return false;
        const size_t len = (size_t(data[i]) << 8) | data[i + 1];
        if (len < 2)
            return false;
        i += len;
    }
    return false;
}

} // namespace

MjpegDecoder::MjpegDecoder() { m_handle = tjInitDecompress(); }

MjpegDecoder::~MjpegDecoder()
{
    if (m_handle)
        tjDestroy(static_cast<tjhandle>(m_handle));
}

bool MjpegDecoder::decode(const uint8_t *data, size_t size, Frame &out, int scaleDenom)
{
    if (!m_handle) {
        m_error = "libjpeg-turbo could not be initialized";
        return false;
    }
    // Reject obviously truncated frames (USB hiccups) before handing them to the decoder.
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8 || !hasFrameHeader(data, size)) {
        m_error = "corrupt MJPEG frame";
        return false;
    }
    auto h = static_cast<tjhandle>(m_handle);
    int w = 0, ht = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(h, data, (unsigned long)size, &w, &ht, &subsamp, &colorspace) != 0) {
        m_error = tjGetErrorStr2(h);
        return false;
    }
    if (w <= 0 || ht <= 0 || w > 16384 || ht > 16384) {
        m_error = "invalid MJPEG dimensions";
        return false;
    }
    if (colorspace != TJCS_YCbCr && colorspace != TJCS_GRAY) {
        m_error = "unsupported MJPEG colorspace";
        return false;
    }
    if (scaleDenom != 1 && scaleDenom != 2 && scaleDenom != 4 && scaleDenom != 8)
        scaleDenom = 1;
    tjscalingfactor sf{1, scaleDenom};
    const int sw = TJSCALED(w, sf), sh = TJSCALED(ht, sf);
    const bool gray = subsamp == TJSAMP_GRAY;
    const int cw = gray ? 0 : tjPlaneWidth(1, sw, subsamp);
    const int ch = gray ? 0 : tjPlaneHeight(1, sh, subsamp);
    if (!out.allocPlanar(sw, sh, cw, ch, !gray)) {
        m_error = "out of memory";
        return false;
    }
    unsigned char *planes[3] = {out.plane[0], out.plane[1], out.plane[2]};
    int strides[3] = {out.stride[0], out.stride[1], out.stride[2]};
    // Fast DCT is visually indistinguishable at webcam quality and noticeably cheaper.
    int rc = tjDecompressToYUVPlanes(h, data, (unsigned long)size, planes, sw, strides, sh,
                                     TJFLAG_FASTDCT | TJFLAG_NOREALLOC);
    if (rc != 0) {
        // Warnings (e.g. missing EOI on a slightly truncated frame) still produce a usable image.
        if (tjGetErrorCode(h) != TJERR_WARNING) {
            m_error = tjGetErrorStr2(h);
            return false;
        }
    }
    out.fullRange = true; // JFIF
    return true;
}

} // namespace cam
