// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Frame.h"
#include "nn/Network.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cam {

class ThreadPool;

// Finds the person in the picture with Google's MediaPipe selfie-segmentation
// model (Apache-2.0, embedded in the binary, runs on the CPU; nothing leaves
// the machine). The coarse 256x144 result is smoothed over time and refined
// against the full-resolution picture with a fast guided filter, so the mask
// follows the real edges of hair and shoulders.
class PersonSegmenter {
public:
    // Loads the embedded model on first use. Returns false if unavailable.
    bool available();
    const std::string &error() const { return m_error; }

    // Writes a mask at the frame's luma resolution (255 = person).
    // softness: 0 (hard edge) .. 1 (very soft).
    bool compute(const Frame &i420, ThreadPool &pool, double softness, std::vector<uint8_t> &mask);

    // Forgets the temporal history (e.g. after a camera switch).
    void reset() { m_prev.clear(); }

private:
    void prepareInput(const Frame &f, ThreadPool &pool);
    void guidedCoefficients();

    nn::Network m_net;
    bool m_tried = false;
    bool m_ok = false;
    std::string m_error;
    int m_nw = 0, m_nh = 0;
    int64_t m_lastRunNs = 0;

    std::vector<float> m_guide; // low-res luma 0..1
    std::vector<float> m_prob;  // smoothed probability
    std::vector<float> m_prev;
    std::vector<float> m_a, m_b, m_t1, m_t2, m_t3, m_t4, m_line;
    std::vector<float> m_rowA, m_rowB;
    std::vector<int32_t> m_cx0, m_cx1;
    std::vector<float> m_cfx;
};

} // namespace cam
