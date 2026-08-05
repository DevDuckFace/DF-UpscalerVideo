"""Convert a spandrel-loadable .pth super-resolution model to ncnn.

Runs offline, as a build-time tool. Nothing from PyTorch ships with the
application: the output is a .param / .bin pair that ncnn loads on its own.
"""

import subprocess
import sys
from pathlib import Path

import torch
import spandrel


def main() -> int:
    if len(sys.argv) < 4:
        print("usage: convert.py <model.pth> <outdir> <pnnx.exe> [tile]")
        return 2

    src = Path(sys.argv[1])
    outdir = Path(sys.argv[2])
    pnnx = Path(sys.argv[3])
    tile = int(sys.argv[4]) if len(sys.argv) > 4 else 128

    outdir.mkdir(parents=True, exist_ok=True)

    descriptor = spandrel.ModelLoader().load_from_file(str(src))
    model = descriptor.model.eval().cpu().float()

    print(f"architecture : {descriptor.architecture.name}")
    print(f"scale        : {descriptor.scale}")
    print(f"purpose      : {descriptor.purpose}")

    # Trace at a fixed tile size. ncnn re-runs the graph at whatever size it is
    # given, so this only has to be representative, not final.
    example = torch.rand(1, descriptor.input_channels, tile, tile)

    with torch.inference_mode():
        reference = model(example)
    print(f"traced shape : {tuple(example.shape)} -> {tuple(reference.shape)}")

    traced = torch.jit.trace(model, example)
    traced_path = outdir / (src.stem + ".pt")
    traced.save(str(traced_path))
    print(f"torchscript  : {traced_path.name}")

    result = subprocess.run(
        [
            str(pnnx),
            str(traced_path),
            f"inputshape=[1,{descriptor.input_channels},{tile},{tile}]",
        ],
        cwd=str(outdir),
        capture_output=True,
        text=True,
    )
    print(result.stdout[-3000:])
    if result.returncode != 0:
        print(result.stderr[-3000:], file=sys.stderr)
        return 1

    param = outdir / (src.stem + ".ncnn.param")
    binf = outdir / (src.stem + ".ncnn.bin")
    if not param.exists() or not binf.exists():
        print("pnnx did not produce ncnn files", file=sys.stderr)
        return 1

    print(f"ncnn param   : {param.name} ({param.stat().st_size} bytes)")
    print(f"ncnn bin     : {binf.name} ({binf.stat().st_size / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
