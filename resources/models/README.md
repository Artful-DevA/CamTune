# Bundled model

`selfie_segmentation_landscape.camnn` is Google's MediaPipe *Selfie
Segmentation* (landscape, 256×144) model, converted from
`mediapipe/modules/selfie_segmentation/selfie_segmentation_landscape.tflite`
(MediaPipe 0.10.14) with `tools/convert_tflite_model.py`. Weights are unchanged
(stored as float16, as in the original).

Copyright Google LLC, licensed under the Apache License 2.0 — see
`LICENSE.Apache-2.0`. It is compiled into the application and runs locally on
the CPU; nothing is downloaded or sent anywhere.
