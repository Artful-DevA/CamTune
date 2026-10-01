// SPDX-License-Identifier: GPL-3.0-or-later
#include "MjpegDecoder.h"

#include <turbojpeg.h>

namespace cam {

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
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
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
