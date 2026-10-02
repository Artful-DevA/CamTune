#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Converts MediaPipe's selfie-segmentation .tflite model into the small
".camnn" format read by src/core/nn/Network.cpp.

The app embeds the converted file, so users never need Python. To regenerate:

    pip install tflite numpy
    pip download --no-deps mediapipe==0.10.14 --only-binary=:all: \
        --python-version 3.11 --platform manylinux_2_17_x86_64
    unzip -j mediapipe-*.whl \
        mediapipe/modules/selfie_segmentation/selfie_segmentation_landscape.tflite
    tools/convert_tflite_model.py selfie_segmentation_landscape.tflite \
        resources/models/selfie_segmentation_landscape.camnn

Format (little endian):
  "CAMNN001"
  u32 tensorCount; per tensor: u32 dims[4] (NHWC, padded with 1),
                               u8 dtype (0 none, 1 f32, 2 f16, 3 i32), u32 count, data
  u32 opCount;     per op: u32 type, u32 nIn, i32 in[nIn], u32 nOut, i32 out[nOut], i32 params[8]
  i32 inputTensor, i32 outputTensor
"""
import struct
import sys

import numpy as np
import tflite

OP_CONV, OP_DWCONV, OP_HSWISH, OP_RELU, OP_MEAN, OP_LOGISTIC, OP_MUL, OP_ADD, OP_RESIZE, OP_TCONV = range(1, 11)


def main(src, dst):
    buf = open(src, 'rb').read()
    m = tflite.Model.GetRootAsModel(buf, 0)
    g = m.Subgraphs(0)
    names = {v: k for k, v in tflite.BuiltinOperator.__dict__.items() if not k.startswith('_')}

    def tensor_data(t):
        tensor = g.Tensors(t)
        b = m.Buffers(tensor.Buffer())
        if b is None or b.DataLength() == 0:
            return None
        raw = b.DataAsNumpy().tobytes()
        dt = {tflite.TensorType.FLOAT32: np.float32, tflite.TensorType.FLOAT16: np.float16,
              tflite.TensorType.INT32: np.int32}[tensor.Type()]
        return np.frombuffer(raw, dtype=dt)

    data = {}
    for t in range(g.TensorsLength()):
        d = tensor_data(t)
        if d is not None:
            data[t] = d

    ops = []
    for i in range(g.OperatorsLength()):
        op = g.Operators(i)
        oc = m.OperatorCodes(op.OpcodeIndex())
        name = names.get(max(oc.BuiltinCode(), oc.DeprecatedBuiltinCode()))
        ins = [int(x) for x in op.InputsAsNumpy()]
        outs = [int(x) for x in op.OutputsAsNumpy()]
        params = [0] * 8
        opt = op.BuiltinOptions()
        if name == 'DEQUANTIZE':
            data[outs[0]] = data[ins[0]]  # resolved offline: weights stay fp16
            continue
        if name == 'CONV_2D':
            o = tflite.Conv2DOptions(); o.Init(opt.Bytes, opt.Pos)
            params[:6] = [o.StrideW(), o.StrideH(), o.Padding(), o.FusedActivationFunction(),
                          o.DilationWFactor(), o.DilationHFactor()]
            kind = OP_CONV
        elif name == 'DEPTHWISE_CONV_2D':
            o = tflite.DepthwiseConv2DOptions(); o.Init(opt.Bytes, opt.Pos)
            params[:7] = [o.StrideW(), o.StrideH(), o.Padding(), o.FusedActivationFunction(),
                          o.DilationWFactor(), o.DilationHFactor(), o.DepthMultiplier()]
            kind = OP_DWCONV
        elif name == 'HARD_SWISH':
            kind = OP_HSWISH
        elif name == 'RELU':
            kind = OP_RELU
        elif name == 'LOGISTIC':
            kind = OP_LOGISTIC
        elif name == 'MEAN':
            o = tflite.ReducerOptions(); o.Init(opt.Bytes, opt.Pos)
            axes = data[ins[1]].tolist()
            if sorted(axes) != [1, 2]:
                raise SystemExit('unsupported MEAN axes %r' % axes)
            params[0] = int(o.KeepDims())
            ins = ins[:1]
            kind = OP_MEAN
        elif name in ('MUL', 'ADD'):
            o = (tflite.MulOptions() if name == 'MUL' else tflite.AddOptions())
            o.Init(opt.Bytes, opt.Pos)
            params[3] = o.FusedActivationFunction()
            kind = OP_MUL if name == 'MUL' else OP_ADD
        elif name == 'RESIZE_BILINEAR':
            o = tflite.ResizeBilinearOptions(); o.Init(opt.Bytes, opt.Pos)
            params[:2] = [int(o.AlignCorners()), int(o.HalfPixelCenters())]
            ins = ins[:1]
            kind = OP_RESIZE
        elif name == 'CUSTOM' and oc.CustomCode() == b'Convolution2DTransposeBias':
            padding, stride_w, stride_h = struct.unpack('<3i', op.CustomOptionsAsNumpy().tobytes()[:12])
            params[:3] = [stride_w, stride_h, padding]
            kind = OP_TCONV
        else:
            raise SystemExit('unsupported op %s' % name)
        ops.append((kind, ins, outs, params))

    used = {t for _, ins, outs, _ in ops for t in ins + outs}
    out = bytearray(b'CAMNN001')
    out += struct.pack('<I', g.TensorsLength())
    for t in range(g.TensorsLength()):
        shape = [int(x) for x in g.Tensors(t).ShapeAsNumpy()] if g.Tensors(t).ShapeLength() else [1]
        dims = ([1] * (4 - len(shape)) + shape) if len(shape) < 4 else shape
        out += struct.pack('<4I', *dims)
        d = data.get(t) if t in used else None
        if d is None:
            out += struct.pack('<BI', 0, 0)
        else:
            dtype = {np.dtype(np.float32): 1, np.dtype(np.float16): 2, np.dtype(np.int32): 3}[d.dtype]
            out += struct.pack('<BI', dtype, d.size) + d.astype(d.dtype.newbyteorder('<')).tobytes()
    out += struct.pack('<I', len(ops))
    for kind, ins, outs, params in ops:
        out += struct.pack('<II', kind, len(ins)) + struct.pack('<%di' % len(ins), *ins)
        out += struct.pack('<I', len(outs)) + struct.pack('<%di' % len(outs), *outs)
        out += struct.pack('<8i', *params)
    out += struct.pack('<ii', g.Inputs(0), g.Outputs(0))
    open(dst, 'wb').write(out)
    print('%d tensors, %d ops, %d bytes -> %s' % (g.TensorsLength(), len(ops), len(out), dst))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
