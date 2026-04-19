#!/usr/bin/env python3
"""
Emit a structurally valid TurboQuant sidecar (.turboquant.bin).

Payload layout — per docs in ui/llama.cpp/llama.cpp/src/turboquant.h:
    header (32 bytes):
        magic "TQNT", version u32, num_layers u32, head_dim u32,
        num_heads u32, num_kv_heads u32, bits_k u8, bits_v u8, 6 bytes pad
    per layer (contiguous, f32):
        hh_k       [D*D]
        hh_v       [D*D]
        codebook_k [2^bits_k * D]
        codebook_v [2^bits_v * D]

Current output: identity Householder per layer, centroids laid out as
equispaced ramps on the first coordinate (so the codebook is recognizable
under a hex dump but quantization error vs fp K/V is large). Lloyd-Max
calibration from real activations is a follow-up.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def build_identity_hh(head_dim: int) -> bytes:
    buf = bytearray(head_dim * head_dim * 4)
    for i in range(head_dim):
        struct.pack_into("<f", buf, (i * head_dim + i) * 4, 1.0)
    return bytes(buf)


def build_ramp_codebook(num_entries: int, head_dim: int) -> bytes:
    # Entry i, coord 0 = (i / (num_entries - 1)) * 2 - 1  in [-1, 1]; other coords = 0.
    # Placeholder: Lloyd-Max on calibration activations replaces this later.
    buf = bytearray(num_entries * head_dim * 4)
    if num_entries == 1:
        return bytes(buf)
    for i in range(num_entries):
        val = (i / (num_entries - 1)) * 2.0 - 1.0
        struct.pack_into("<f", buf, (i * head_dim) * 4, val)
    return bytes(buf)


def encode(
    out_path: Path,
    num_layers: int,
    head_dim: int,
    num_heads: int,
    num_kv_heads: int,
    bits_k: int,
    bits_v: int,
) -> None:
    if not (1 <= bits_k <= 8 and 1 <= bits_v <= 8):
        raise ValueError("bits_k and bits_v must be in [1, 8]")
    if num_layers <= 0 or head_dim <= 0:
        raise ValueError("num_layers and head_dim must be positive")

    header = struct.pack(
        "<4sIIIIIBB6x",
        b"TQNT",
        1,
        num_layers,
        head_dim,
        num_heads,
        num_kv_heads,
        bits_k,
        bits_v,
    )
    assert len(header) == 32

    identity = build_identity_hh(head_dim)
    codebook_k = build_ramp_codebook(1 << bits_k, head_dim)
    codebook_v = build_ramp_codebook(1 << bits_v, head_dim)
    per_layer = identity + identity + codebook_k + codebook_v

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(header)
        for _ in range(num_layers):
            f.write(per_layer)

    total = 32 + num_layers * len(per_layer)
    print(f"Wrote {out_path} ({total} bytes, {num_layers} layers, "
          f"head_dim={head_dim}, bits_k={bits_k}, bits_v={bits_v})")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path, help="output .turboquant.bin path")
    ap.add_argument("--num-layers", required=True, type=int)
    ap.add_argument("--head-dim", required=True, type=int)
    ap.add_argument("--num-heads", required=True, type=int)
    ap.add_argument("--num-kv-heads", required=True, type=int)
    ap.add_argument("--bits-k", type=int, default=8)
    ap.add_argument("--bits-v", type=int, default=8)
    args = ap.parse_args(argv)

    encode(
        out_path=args.out,
        num_layers=args.num_layers,
        head_dim=args.head_dim,
        num_heads=args.num_heads,
        num_kv_heads=args.num_kv_heads,
        bits_k=args.bits_k,
        bits_v=args.bits_v,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
