// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cam {
class ThreadPool;
}

namespace cam::nn {

// A tiny float32 inference engine for small convolutional networks stored in
// the ".camnn" format (see tools/convert_tflite_model.py). It implements just
// the operations the selfie-segmentation model needs, runs entirely on the
// CPU and has no dependencies. Tensors are NHWC with batch 1.
class Network {
public:
    bool load(const uint8_t *data, size_t size, std::string &error);
    bool isLoaded() const { return !m_ops.empty(); }

    int inputWidth() const;
    int inputHeight() const;
    int inputChannels() const;
    int outputWidth() const;
    int outputHeight() const;

    float *input();
    const float *output() const;

    // Runs the network; rows of large layers are split across the pool.
    void run(ThreadPool *pool);

private:
    struct Tensor {
        int dims[4] = {1, 1, 1, 1}; // as stored (NHWC, or OHWI for weights)
        int h = 1, w = 1, c = 1;    // activation view (batch 1)
        bool constant = false;
        std::vector<float> data;
        size_t size() const { return size_t(h) * w * c; }
    };
    struct Op {
        int type = 0;
        std::vector<int> in, out;
        int p[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<float> weights; // re-laid-out for the kernels
        int kh = 0, kw = 0;
    };

    bool prepare(Op &op, std::string &error);
    void conv(const Op &op, ThreadPool *pool);
    void depthwise(const Op &op, ThreadPool *pool);
    void transposeConv(const Op &op);
    void resize(const Op &op);
    void mean(const Op &op);
    void binary(const Op &op, bool mul);
    void unary(const Op &op);

    std::vector<Tensor> m_tensors;
    std::vector<Op> m_ops;
    int m_input = -1, m_output = -1;
};

} // namespace cam::nn
