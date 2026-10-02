// SPDX-License-Identifier: GPL-3.0-or-later
#include "Network.h"

#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cam::nn {

namespace {

enum OpType { Conv = 1, DwConv, HardSwish, Relu, Mean, Logistic, Mul, Add, Resize, TConv };
enum Activation { ActNone = 0, ActRelu = 1, ActReluN1To1 = 2, ActRelu6 = 3 };
enum Padding { PadSame = 0, PadValid = 1 }; // tflite Conv2DOptions padding enum

class Reader {
public:
    Reader(const uint8_t *d, size_t n) : m_d(d), m_n(n) {}
    bool read(void *dst, size_t n)
    {
        if (m_pos + n > m_n)
            return false;
        std::memcpy(dst, m_d + m_pos, n);
        m_pos += n;
        return true;
    }
    template <typename T>
    bool get(T &v) { return read(&v, sizeof v); }

private:
    const uint8_t *m_d;
    size_t m_n;
    size_t m_pos = 0;
};

float halfToFloat(uint16_t h)
{
    const uint32_t sign = uint32_t(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else { // subnormal
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3ff;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000 | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

inline float activate(float v, int act)
{
    switch (act) {
    case ActRelu: return v > 0 ? v : 0;
    case ActRelu6: return std::min(std::max(v, 0.0f), 6.0f);
    case ActReluN1To1: return std::min(std::max(v, -1.0f), 1.0f);
    default: return v;
    }
}

// TensorFlow "SAME" padding (the extra pixel goes after).
int padBefore(int in, int out, int stride, int k, int dilation, int padding)
{
    if (padding != PadSame)
        return 0;
    const int effK = (k - 1) * dilation + 1;
    return std::max((out - 1) * stride + effK - in, 0) / 2;
}

} // namespace

bool Network::load(const uint8_t *data, size_t size, std::string &error)
{
    m_tensors.clear();
    m_ops.clear();
    Reader r(data, size);
    char magic[8];
    if (!r.read(magic, 8) || std::memcmp(magic, "CAMNN001", 8) != 0) {
        error = "not a camnn model";
        return false;
    }
    uint32_t tensorCount = 0;
    if (!r.get(tensorCount) || tensorCount == 0 || tensorCount > 100000) {
        error = "bad tensor count";
        return false;
    }
    m_tensors.resize(tensorCount);
    for (auto &t : m_tensors) {
        uint32_t dims[4];
        uint8_t dtype = 0;
        uint32_t count = 0;
        if (!r.read(dims, sizeof dims) || !r.get(dtype) || !r.get(count)) {
            error = "truncated tensor table";
            return false;
        }
        size_t total = 1;
        for (int i = 0; i < 4; ++i) {
            if (dims[i] == 0 || dims[i] > 65536) {
                error = "bad tensor shape";
                return false;
            }
            t.dims[i] = int(dims[i]);
            total *= dims[i];
        }
        if (total > (size_t(1) << 26)) {
            error = "tensor too large";
            return false;
        }
        t.h = t.dims[1];
        t.w = t.dims[2];
        t.c = t.dims[3];
        if (dtype == 0) {
            t.data.assign(total, 0.0f);
            continue;
        }
        if (count != total) {
            error = "tensor data size mismatch";
            return false;
        }
        t.constant = true;
        t.data.resize(count);
        if (dtype == 1) {
            if (!r.read(t.data.data(), count * sizeof(float))) {
                error = "truncated tensor data";
                return false;
            }
        } else if (dtype == 2) {
            std::vector<uint16_t> half(count);
            if (!r.read(half.data(), count * sizeof(uint16_t))) {
                error = "truncated tensor data";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i)
                t.data[i] = halfToFloat(half[i]);
        } else if (dtype == 3) {
            std::vector<int32_t> ints(count);
            if (!r.read(ints.data(), count * sizeof(int32_t))) {
                error = "truncated tensor data";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i)
                t.data[i] = float(ints[i]);
        } else {
            error = "unknown tensor type";
            return false;
        }
    }

    uint32_t opCount = 0;
    if (!r.get(opCount) || opCount == 0 || opCount > 100000) {
        error = "bad op count";
        return false;
    }
    auto validTensor = [&](int idx) { return idx >= 0 && size_t(idx) < m_tensors.size(); };
    m_ops.resize(opCount);
    for (auto &op : m_ops) {
        uint32_t type = 0, nIn = 0, nOut = 0;
        if (!r.get(type) || !r.get(nIn) || nIn > 8) {
            error = "truncated op";
            return false;
        }
        op.type = int(type);
        op.in.resize(nIn);
        for (auto &i : op.in)
            if (!r.get(i)) {
                error = "truncated op";
                return false;
            }
        if (!r.get(nOut) || nOut != 1) {
            error = "bad op outputs";
            return false;
        }
        op.out.resize(1);
        if (!r.get(op.out[0]) || !r.read(op.p, sizeof op.p)) {
            error = "truncated op";
            return false;
        }
        for (int i : op.in)
            if (i >= 0 && !validTensor(i)) {
                error = "bad tensor reference";
                return false;
            }
        if (!validTensor(op.out[0]) || op.in.empty() || !validTensor(op.in[0])) {
            error = "bad tensor reference";
            return false;
        }
        if (!prepare(op, error))
            return false;
    }
    if (!r.get(m_input) || !r.get(m_output) || !validTensor(m_input) || !validTensor(m_output)) {
        error = "bad graph inputs";
        return false;
    }
    return true;
}

bool Network::prepare(Op &op, std::string &error)
{
    auto need = [&](size_t n) {
        if (op.in.size() < n) {
            error = "op has too few inputs";
            return false;
        }
        for (size_t i = 0; i < n; ++i)
            if (op.in[i] < 0) {
                error = "missing op input";
                return false;
            }
        return true;
    };
    const Tensor &out = m_tensors[size_t(op.out[0])];
    switch (op.type) {
    case Conv: {
        if (!need(3))
            return false;
        const Tensor &x = m_tensors[size_t(op.in[0])];
        const Tensor &w = m_tensors[size_t(op.in[1])];
        // OHWI -> [kh][kw][in][out] so the inner loop runs over output channels.
        const int co = w.dims[0], kh = w.dims[1], kw = w.dims[2], ci = w.dims[3];
        if (ci != x.c || co != out.c || m_tensors[size_t(op.in[2])].data.size() != size_t(co)) {
            error = "conv shape mismatch";
            return false;
        }
        op.kh = kh;
        op.kw = kw;
        op.weights.resize(w.data.size());
        for (int o = 0; o < co; ++o)
            for (int y = 0; y < kh; ++y)
                for (int xk = 0; xk < kw; ++xk)
                    for (int i = 0; i < ci; ++i)
                        op.weights[((size_t(y) * kw + xk) * ci + i) * co + o] =
                            w.data[((size_t(o) * kh + y) * kw + xk) * ci + i];
        if (op.p[0] <= 0 || op.p[1] <= 0)
            op.p[0] = op.p[1] = 1;
        if (op.p[4] <= 0)
            op.p[4] = 1;
        if (op.p[5] <= 0)
            op.p[5] = 1;
        return true;
    }
    case DwConv: {
        if (!need(3))
            return false;
        const Tensor &x = m_tensors[size_t(op.in[0])];
        const Tensor &w = m_tensors[size_t(op.in[1])];
        if (w.dims[3] != x.c || out.c != x.c || m_tensors[size_t(op.in[2])].data.size() != size_t(x.c)) {
            error = "depthwise shape mismatch (only multiplier 1 is supported)";
            return false;
        }
        op.kh = w.dims[1];
        op.kw = w.dims[2];
        op.weights = w.data; // [1][kh][kw][c] is already the convenient layout
        if (op.p[0] <= 0 || op.p[1] <= 0)
            op.p[0] = op.p[1] = 1;
        if (op.p[4] <= 0)
            op.p[4] = 1;
        if (op.p[5] <= 0)
            op.p[5] = 1;
        return true;
    }
    case TConv: {
        if (!need(3))
            return false;
        const Tensor &x = m_tensors[size_t(op.in[0])];
        const Tensor &w = m_tensors[size_t(op.in[1])];
        const int co = w.dims[0], kh = w.dims[1], kw = w.dims[2], ci = w.dims[3];
        if (ci != x.c || co != out.c || m_tensors[size_t(op.in[2])].data.size() != size_t(co)) {
            error = "transpose conv shape mismatch";
            return false;
        }
        op.kh = kh;
        op.kw = kw;
        op.weights = w.data;
        if (op.p[0] <= 0 || op.p[1] <= 0)
            op.p[0] = op.p[1] = 1;
        return true;
    }
    case Mul:
    case Add: {
        if (!need(2))
            return false;
        const Tensor &a = m_tensors[size_t(op.in[0])];
        const Tensor &b = m_tensors[size_t(op.in[1])];
        auto ok = [&](int da, int db, int dout) { return (da == dout || da == 1) && (db == dout || db == 1); };
        if (!ok(a.h, b.h, out.h) || !ok(a.w, b.w, out.w) || !ok(a.c, b.c, out.c)) {
            error = "broadcast shape mismatch";
            return false;
        }
        return true;
    }
    case Resize:
    case Mean:
    case HardSwish:
    case Relu:
    case Logistic:
        return need(1);
    default:
        error = "unknown op type";
        return false;
    }
}

int Network::inputWidth() const { return m_input >= 0 ? m_tensors[size_t(m_input)].w : 0; }
int Network::inputHeight() const { return m_input >= 0 ? m_tensors[size_t(m_input)].h : 0; }
int Network::inputChannels() const { return m_input >= 0 ? m_tensors[size_t(m_input)].c : 0; }
int Network::outputWidth() const { return m_output >= 0 ? m_tensors[size_t(m_output)].w : 0; }
int Network::outputHeight() const { return m_output >= 0 ? m_tensors[size_t(m_output)].h : 0; }
float *Network::input() { return m_tensors[size_t(m_input)].data.data(); }
const float *Network::output() const { return m_tensors[size_t(m_output)].data.data(); }

void Network::conv(const Op &op, ThreadPool *pool)
{
    const Tensor &x = m_tensors[size_t(op.in[0])];
    const float *bias = m_tensors[size_t(op.in[2])].data.data();
    Tensor &y = m_tensors[size_t(op.out[0])];
    const int sw = op.p[0], sh = op.p[1], act = op.p[3], dw = op.p[4], dh = op.p[5];
    const int ci = x.c, co = y.c, kh = op.kh, kw = op.kw;
    const int padT = padBefore(x.h, y.h, sh, kh, dh, op.p[2]);
    const int padL = padBefore(x.w, y.w, sw, kw, dw, op.p[2]);
    const float *W = op.weights.data();
    const float *in = x.data.data();
    float *outp = y.data.data();

    auto rows = [&](int yb, int ye) {
        std::vector<float> acc(static_cast<size_t>(co));
        for (int oy = yb; oy < ye; ++oy) {
            for (int ox = 0; ox < y.w; ++ox) {
                std::copy(bias, bias + co, acc.begin());
                float *a = acc.data();
                for (int ky = 0; ky < kh; ++ky) {
                    const int iy = oy * sh + ky * dh - padT;
                    if (iy < 0 || iy >= x.h)
                        continue;
                    for (int kx = 0; kx < kw; ++kx) {
                        const int ix = ox * sw + kx * dw - padL;
                        if (ix < 0 || ix >= x.w)
                            continue;
                        const float *px = in + (size_t(iy) * x.w + ix) * ci;
                        const float *wk = W + (size_t(ky) * kw + kx) * ci * co;
                        for (int i = 0; i < ci; ++i) {
                            const float v = px[i];
                            const float *wr = wk + size_t(i) * co;
                            for (int o = 0; o < co; ++o)
                                a[o] += v * wr[o];
                        }
                    }
                }
                float *dst = outp + (size_t(oy) * y.w + ox) * co;
                for (int o = 0; o < co; ++o)
                    dst[o] = activate(a[o], act);
            }
        }
    };
    if (pool && size_t(y.h) * y.w * co * ci * kh * kw > 200000)
        pool->parallelFor(y.h, rows, 2);
    else
        rows(0, y.h);
}

void Network::depthwise(const Op &op, ThreadPool *pool)
{
    const Tensor &x = m_tensors[size_t(op.in[0])];
    const float *bias = m_tensors[size_t(op.in[2])].data.data();
    Tensor &y = m_tensors[size_t(op.out[0])];
    const int sw = op.p[0], sh = op.p[1], act = op.p[3], dw = op.p[4], dh = op.p[5];
    const int c = x.c, kh = op.kh, kw = op.kw;
    const int padT = padBefore(x.h, y.h, sh, kh, dh, op.p[2]);
    const int padL = padBefore(x.w, y.w, sw, kw, dw, op.p[2]);
    const float *W = op.weights.data();
    const float *in = x.data.data();
    float *outp = y.data.data();

    auto rows = [&](int yb, int ye) {
        for (int oy = yb; oy < ye; ++oy)
            for (int ox = 0; ox < y.w; ++ox) {
                float *dst = outp + (size_t(oy) * y.w + ox) * c;
                std::copy(bias, bias + c, dst);
                for (int ky = 0; ky < kh; ++ky) {
                    const int iy = oy * sh + ky * dh - padT;
                    if (iy < 0 || iy >= x.h)
                        continue;
                    for (int kx = 0; kx < kw; ++kx) {
                        const int ix = ox * sw + kx * dw - padL;
                        if (ix < 0 || ix >= x.w)
                            continue;
                        const float *px = in + (size_t(iy) * x.w + ix) * c;
                        const float *wk = W + (size_t(ky) * kw + kx) * c;
                        for (int i = 0; i < c; ++i)
                            dst[i] += px[i] * wk[i];
                    }
                }
                for (int i = 0; i < c; ++i)
                    dst[i] = activate(dst[i], act);
            }
    };
    if (pool && size_t(y.h) * y.w * c * kh * kw > 200000)
        pool->parallelFor(y.h, rows, 2);
    else
        rows(0, y.h);
}

void Network::transposeConv(const Op &op)
{
    const Tensor &x = m_tensors[size_t(op.in[0])];
    const float *bias = m_tensors[size_t(op.in[2])].data.data();
    Tensor &y = m_tensors[size_t(op.out[0])];
    const int sw = op.p[0], sh = op.p[1];
    const int ci = x.c, co = y.c, kh = op.kh, kw = op.kw;
    // MediaPipe's Convolution2DTransposeBias uses TensorFlow SAME semantics.
    const int padT = std::max((x.h - 1) * sh + kh - y.h, 0) / 2;
    const int padL = std::max((x.w - 1) * sw + kw - y.w, 0) / 2;
    const float *W = op.weights.data(); // [co][kh][kw][ci]
    float *outp = y.data.data();
    for (size_t i = 0; i < y.size(); i += size_t(co))
        std::copy(bias, bias + co, outp + i);
    for (int iy = 0; iy < x.h; ++iy)
        for (int ix = 0; ix < x.w; ++ix) {
            const float *px = x.data.data() + (size_t(iy) * x.w + ix) * ci;
            for (int ky = 0; ky < kh; ++ky) {
                const int oy = iy * sh + ky - padT;
                if (oy < 0 || oy >= y.h)
                    continue;
                for (int kx = 0; kx < kw; ++kx) {
                    const int ox = ix * sw + kx - padL;
                    if (ox < 0 || ox >= y.w)
                        continue;
                    float *dst = outp + (size_t(oy) * y.w + ox) * co;
                    for (int o = 0; o < co; ++o) {
                        const float *wr = W + ((size_t(o) * kh + ky) * kw + kx) * ci;
                        float s = 0;
                        for (int i = 0; i < ci; ++i)
                            s += px[i] * wr[i];
                        dst[o] += s;
                    }
                }
            }
        }
}

void Network::resize(const Op &op)
{
    const Tensor &x = m_tensors[size_t(op.in[0])];
    Tensor &y = m_tensors[size_t(op.out[0])];
    const bool align = op.p[0] != 0, half = op.p[1] != 0;
    auto scale = [&](int in, int out) {
        return (align && out > 1) ? float(in - 1) / float(out - 1) : float(in) / float(out);
    };
    const float sy = scale(x.h, y.h), sx = scale(x.w, y.w);
    const int c = x.c;
    for (int oy = 0; oy < y.h; ++oy) {
        const float fy = half ? (oy + 0.5f) * sy - 0.5f : oy * sy;
        const int y0 = std::max(int(std::floor(fy)), 0);
        const int y1 = std::min(int(std::ceil(fy)), x.h - 1);
        const float ly = fy - std::floor(fy);
        for (int ox = 0; ox < y.w; ++ox) {
            const float fx = half ? (ox + 0.5f) * sx - 0.5f : ox * sx;
            const int x0 = std::max(int(std::floor(fx)), 0);
            const int x1 = std::min(int(std::ceil(fx)), x.w - 1);
            const float lx = fx - std::floor(fx);
            const float *p00 = x.data.data() + (size_t(y0) * x.w + x0) * c;
            const float *p01 = x.data.data() + (size_t(y0) * x.w + x1) * c;
            const float *p10 = x.data.data() + (size_t(y1) * x.w + x0) * c;
            const float *p11 = x.data.data() + (size_t(y1) * x.w + x1) * c;
            float *dst = y.data.data() + (size_t(oy) * y.w + ox) * c;
            for (int i = 0; i < c; ++i) {
                const float top = p00[i] + (p01[i] - p00[i]) * lx;
                const float bot = p10[i] + (p11[i] - p10[i]) * lx;
                dst[i] = top + (bot - top) * ly;
            }
        }
    }
}

void Network::mean(const Op &op)
{
    const Tensor &x = m_tensors[size_t(op.in[0])];
    Tensor &y = m_tensors[size_t(op.out[0])];
    std::fill(y.data.begin(), y.data.end(), 0.0f);
    const int c = x.c;
    for (size_t p = 0; p < size_t(x.h) * x.w; ++p)
        for (int i = 0; i < c; ++i)
            y.data[size_t(i)] += x.data[p * c + i];
    const float inv = 1.0f / float(size_t(x.h) * x.w);
    for (int i = 0; i < c; ++i)
        y.data[size_t(i)] *= inv;
}

void Network::binary(const Op &op, bool mul)
{
    const Tensor &a = m_tensors[size_t(op.in[0])];
    const Tensor &b = m_tensors[size_t(op.in[1])];
    Tensor &y = m_tensors[size_t(op.out[0])];
    const int act = op.p[3];
    const bool same = a.h == b.h && a.w == b.w && a.c == b.c;
    if (same) {
        const size_t n = y.size();
        for (size_t i = 0; i < n; ++i)
            y.data[i] = activate(mul ? a.data[i] * b.data[i] : a.data[i] + b.data[i], act);
        return;
    }
    for (int yy = 0; yy < y.h; ++yy)
        for (int xx = 0; xx < y.w; ++xx) {
            const float *pa = a.data.data() + (size_t(a.h == 1 ? 0 : yy) * a.w + (a.w == 1 ? 0 : xx)) * a.c;
            const float *pb = b.data.data() + (size_t(b.h == 1 ? 0 : yy) * b.w + (b.w == 1 ? 0 : xx)) * b.c;
            float *dst = y.data.data() + (size_t(yy) * y.w + xx) * y.c;
            for (int i = 0; i < y.c; ++i) {
                const float va = pa[a.c == 1 ? 0 : i], vb = pb[b.c == 1 ? 0 : i];
                dst[i] = activate(mul ? va * vb : va + vb, act);
            }
        }
}

void Network::unary(const Op &op)
{
    const Tensor &x = m_tensors[size_t(op.in[0])];
    Tensor &y = m_tensors[size_t(op.out[0])];
    const size_t n = std::min(x.size(), y.size());
    const float *s = x.data.data();
    float *d = y.data.data();
    switch (op.type) {
    case HardSwish:
        for (size_t i = 0; i < n; ++i)
            d[i] = s[i] * std::min(std::max(s[i] + 3.0f, 0.0f), 6.0f) * (1.0f / 6.0f);
        break;
    case Relu:
        for (size_t i = 0; i < n; ++i)
            d[i] = s[i] > 0 ? s[i] : 0;
        break;
    case Logistic:
        for (size_t i = 0; i < n; ++i)
            d[i] = 1.0f / (1.0f + std::exp(-s[i]));
        break;
    default:
        break;
    }
}

void Network::run(ThreadPool *pool)
{
    for (const Op &op : m_ops) {
        switch (op.type) {
        case Conv: conv(op, pool); break;
        case DwConv: depthwise(op, pool); break;
        case TConv: transposeConv(op); break;
        case Resize: resize(op); break;
        case Mean: mean(op); break;
        case Mul: binary(op, true); break;
        case Add: binary(op, false); break;
        default: unary(op); break;
        }
    }
}

} // namespace cam::nn
