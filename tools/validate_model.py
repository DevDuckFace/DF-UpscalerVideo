"""Compare the converted ncnn model against the original PyTorch model.

A conversion that loads but computes something else is worse than one that
fails outright, so this checks the numbers rather than trusting the toolchain.
"""

import os
import sys
from pathlib import Path

# The embedded Python that ships with ComfyUI uses a ._pth file, which makes it
# ignore PYTHONPATH. Packages installed with --target have to be put on the
# path explicitly.
#
# Appended, not prepended: the tools directory also contains the torch build
# that came along with pnnx, and letting that shadow the host torch breaks
# torchvision's operator registration.
_tools = os.environ.get("DFU_TOOLS_DIR")
if _tools:
    sys.path.append(_tools)

import numpy as np
import torch
import spandrel
import ncnn


def main() -> int:
    pth = Path(sys.argv[1])
    param = Path(sys.argv[2])
    binf = Path(sys.argv[3])
    size = int(sys.argv[4]) if len(sys.argv) > 4 else 128

    descriptor = spandrel.ModelLoader().load_from_file(str(pth))
    model = descriptor.model.eval().cpu().float()

    rng = np.random.default_rng(1234)
    chw = rng.random((3, size, size), dtype=np.float32)

    with torch.inference_mode():
        torch_out = model(torch.from_numpy(chw)[None, ...])[0].numpy()

    net = ncnn.Net()
    net.opt.use_vulkan_compute = False
    if net.load_param(str(param)) != 0:
        print("ncnn load_param failed", file=sys.stderr)
        return 1
    if net.load_model(str(binf)) != 0:
        print("ncnn load_model failed", file=sys.stderr)
        return 1

    # ncnn.Mat built from a 3D numpy array reads it as CHW, matching torch.
    #
    # The Mat wraps the buffer without taking ownership, so the array has to
    # stay referenced until after extract(). Passing a temporary here yields
    # nondeterministic garbage, which is exactly what it looks like when a
    # conversion appears to have failed.
    source = np.ascontiguousarray(chw)
    mat_in = ncnn.Mat(source)
    ex = net.create_extractor()
    ex.input("in0", mat_in)
    ret, mat_out = ex.extract("out0")
    del source
    if ret != 0:
        print("ncnn extract failed", file=sys.stderr)
        return 1

    ncnn_out = np.array(mat_out)
    # ncnn hands back HWC; torch works in CHW.
    if ncnn_out.ndim == 3 and ncnn_out.shape[-1] == torch_out.shape[0]:
        ncnn_out = ncnn_out.transpose(2, 0, 1)

    print(f"torch out shape : {torch_out.shape}")
    print(f"ncnn  out shape : {ncnn_out.shape}")

    if ncnn_out.shape != torch_out.shape:
        print("SHAPE MISMATCH", file=sys.stderr)
        return 1

    diff = np.abs(torch_out - ncnn_out)
    denom = max(float(torch_out.max() - torch_out.min()), 1e-6)
    psnr = 20.0 * np.log10(denom / max(float(np.sqrt((diff**2).mean())), 1e-12))

    print(f"max abs diff    : {diff.max():.6f}")
    print(f"mean abs diff   : {diff.mean():.6f}")
    print(f"PSNR vs torch   : {psnr:.1f} dB")

    # pnnx writes fp16 weights for ncnn, as the stock Real-ESRGAN ncnn models
    # do, so a bit-exact match is not the bar. At 8-bit output, 0.02 is about
    # five levels out of 255 and 50 dB is well past visually identical.
    ok = diff.max() < 0.02 and psnr > 50.0
    print("RESULT          :", "MATCH" if ok else "MISMATCH")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
