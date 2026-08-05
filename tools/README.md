# tools/

Offline model conversion. **Nothing here ships with the application** — the
output is a `.param` / `.bin` pair that ncnn loads on its own, so the
specification's ban on PyTorch inside the product still holds.

## Adding a community model

Most models on [OpenModelDB](https://openmodeldb.info) are distributed as
PyTorch `.pth`. ncnn needs `.param` + `.bin`.

### 1. Requirements

Any Python with `torch` and `spandrel`, plus `pnnx` and `ncnn` installed
somewhere that does not disturb it:

```bash
python -m pip install --target ./pytools pnnx ncnn
```

`spandrel` identifies the architecture from the checkpoint, so the model class
does not have to be known in advance.

### 2. Convert

```bash
python tools/convert_model.py model.pth outdir path/to/pnnx.exe 128
```

The last argument is the tile size used for tracing. It only has to be
representative — the graph stays size-agnostic, which step 3 verifies.

### 3. Verify

Never install a converted model without this. A conversion that loads but
computes something else is worse than one that fails outright.

```bash
set DFU_TOOLS_DIR=./pytools
python tools/validate_model.py model.pth outdir/model.ncnn.param outdir/model.ncnn.bin 192
```

It runs the same input through PyTorch and ncnn and compares. Expect ~60 dB
PSNR: pnnx writes fp16 weights, as the stock Real-ESRGAN ncnn models do, so a
bit-exact match is not the bar.

Run it at several sizes. Matching at one size but not another means the trace
baked in fixed shapes, and the model will produce garbage on real footage.

### 4. Install

Copy the pair into `models/`, named so the scale is readable:

```
models/1x-archivist-antilines.param
models/1x-archivist-antilines.bin
```

The application scans that folder at startup. A leading `1x-`/`2x-`/`4x-` or a
trailing `-x2`/`-x4` tells it which scale factors the weights provide, and the
settings panel offers only those. No code changes are needed.

## Which architectures work

| Architecture | Works | Notes |
|---|---|---|
| ESRGAN / RRDBNet | yes | The classic. Heavier. |
| RealESRGAN Compact (SRVGGNetCompact) | yes | What `realesr-animevideov3` uses. Fast. |
| SPAN, RealPLKSR | usually | Plain convolutional; worth trying. |
| DAT, SwinIR, HAT | no | Transformers. ncnn conversion generally fails, and they are far too slow for video on a mid-range GPU. |

## A trap worth knowing about

`ncnn.Mat(numpy_array)` wraps the buffer **without taking ownership**. Passing
a temporary — `ncnn.Mat(arr.copy())` — leaves the Mat pointing at freed memory
and produces nondeterministic garbage that looks exactly like a failed
conversion. Keep a reference alive until after `extract()`.

Also: a 3D numpy array is read as **CHW**, matching PyTorch, not HWC.
