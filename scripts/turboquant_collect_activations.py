#!/usr/bin/env python3
"""
Drive llama-server with --turboquant-calibrate-dir to dump pre-quant K/V
activations for Lloyd-Max calibration.

Usage:
    python scripts/turboquant_collect_activations.py \
        --out-dir tmp/calibration \
        [--sidecar tmp/Qwen3.5-9B.Q5_K_M.turboquant.bin] \
        [--num-prompts 4]

Per-layer output files are appended by the llama-server write hook:
    <out-dir>/k.layer_XXX.f32
    <out-dir>/v.layer_XXX.f32

Each file is a raw float32 stream of K (or V) tensors, shape
    (head_dim * num_kv_heads, num_tokens_collected_across_all_batches)
laid out contiguously per-batch as ggml stores them.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import requests

MODEL_PATH = "/home/jmazz/Projects/QuantClaw/providers.local/qwen3.5-9B-claude4.6-distillation/Qwen3.5-9B.Q5_K_M.gguf"
LLAMA_SERVER = "/home/jmazz/Projects/QuantClaw/build-cmake43/bin/llama-server"
DEFAULT_SIDECAR = "/home/jmazz/Projects/QuantClaw/tmp/Qwen3.5-9B.Q5_K_M.turboquant.bin"
PORT = 39201

CALIBRATION_PROMPTS = [
    "Summarize the proof of the fundamental theorem of algebra in about 200 words, "
    "then state its consequences for polynomial factorization over the complex numbers. "
    "Include one example with coefficients in Q[i].",
    "Describe the architecture of a Transformer decoder layer: attention, layer norm "
    "placement, MLP, residual connections. Then explain why multi-head attention was "
    "originally preferred over grouped-query attention, and what changed.",
    "Explain in depth the operational difference between Python's asyncio event loop "
    "and a thread-pool executor. Cover GIL interaction, when to use each, and give "
    "a worked example involving a blocking I/O call.",
    "Walk through the proof that halting is undecidable via diagonalization over "
    "Turing machines. Then explain how Rice's theorem generalizes this, with one "
    "concrete example of an undecidable semantic property.",
]


def wait_for_health(url: str, timeout: float = 120.0) -> bool:
    start = time.time()
    while time.time() - start < timeout:
        try:
            if requests.get(f"{url}/health", timeout=1).status_code == 200:
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", required=True, type=Path)
    ap.add_argument("--sidecar", default=DEFAULT_SIDECAR)
    ap.add_argument("--num-prompts", type=int, default=len(CALIBRATION_PROMPTS))
    ap.add_argument("--max-tokens", type=int, default=32)
    ap.add_argument("--ctx-size", type=int, default=4096)
    args = ap.parse_args()

    if not Path(args.sidecar).exists():
        print(f"sidecar missing: {args.sidecar}", file=sys.stderr)
        return 1

    if args.out_dir.exists():
        shutil.rmtree(args.out_dir)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    subprocess.run(
        ["fuser", "-k", f"{PORT}/tcp"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    cmd = [
        LLAMA_SERVER,
        "-m", MODEL_PATH,
        "--host", "127.0.0.1",
        "--port", str(PORT),
        "--parallel", "1",
        "--ctx-size", str(args.ctx_size),
        "--turboquant", args.sidecar,
        "--turboquant-bits-k", "8",
        "--turboquant-bits-v", "8",
        "--turboquant-calibrate-dir", str(args.out_dir.resolve()),
    ]

    stderr_path = args.out_dir / "server.stderr.log"
    with stderr_path.open("wb") as stderr_f:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=stderr_f)

    try:
        url = f"http://127.0.0.1:{PORT}"
        if not wait_for_health(url):
            print("server failed to become ready", file=sys.stderr)
            return 1

        prompts = CALIBRATION_PROMPTS[:args.num_prompts]
        for i, prompt in enumerate(prompts):
            print(f"[{i+1}/{len(prompts)}] sending calibration prompt (chars={len(prompt)})...")
            payload = {
                "messages": [{"role": "user", "content": prompt}],
                "temperature": 0,
                "max_tokens": args.max_tokens,
                "seed": 42,
            }
            r = requests.post(f"{url}/v1/chat/completions", json=payload, timeout=600)
            r.raise_for_status()
            data = r.json()
            usage = data.get("usage", {})
            print(f"  prompt_tokens={usage.get('prompt_tokens')} completion_tokens={usage.get('completion_tokens')}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    total = 0
    for p in sorted(args.out_dir.glob("*.f32")):
        size = p.stat().st_size
        total += size
        print(f"{p.name}: {size / 1024 / 1024:.2f} MiB ({size // 4} f32 values)")
    print(f"total: {total / 1024 / 1024:.2f} MiB across {len(list(args.out_dir.glob('*.f32')))} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
