// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Frame.h"

#include <string>

namespace cam {

// Decodes MJPEG webcam frames straight to planar YUV with libjpeg-turbo.
// No RGB conversion happens: the planes are sampled directly by the processor.
class MjpegDecoder {
public:
    MjpegDecoder();
    ~MjpegDecoder();
    MjpegDecoder(const MjpegDecoder &) = delete;
    MjpegDecoder &operator=(const MjpegDecoder &) = delete;

    // scaleDenom: 1, 2, 4 or 8. Decoding at a reduced scale is used for graceful
    // degradation when the output is much smaller than the camera frame.
    bool decode(const uint8_t *data, size_t size, Frame &out, int scaleDenom = 1);
    const std::string &lastError() const { return m_error; }

private:
    void *m_handle = nullptr;
    std::string m_error;
};

} // namespace cam
