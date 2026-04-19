import os
import subprocess
import sys
import time
import json
import requests
import math

MODEL_PATH = "/home/jmazz/Projects/QuantClaw/providers.local/qwen3.5-9B-claude4.6-distillation/Qwen3.5-9B.Q5_K_M.gguf"
SIDECAR_PATH = "/home/jmazz/Projects/QuantClaw/tmp/Qwen3.5-9B.Q5_K_M.turboquant.bin"
LLAMA_SERVER = "/home/jmazz/Projects/QuantClaw/build-cmake43/bin/llama-server"
TMP_DIR = "/home/jmazz/Projects/QuantClaw/tmp"
ENCODER = "/home/jmazz/Projects/QuantClaw/scripts/turboquant_encode.py"

SIDECAR_NUM_LAYERS   = 24
SIDECAR_HEAD_DIM     = 256
SIDECAR_NUM_HEADS    = 32
SIDECAR_NUM_KV_HEADS = 4
SIDECAR_BITS_K       = 8
SIDECAR_BITS_V       = 8

os.makedirs(TMP_DIR, exist_ok=True)


def cleanup_port(port):
    subprocess.run(
        ["fuser", "-k", f"{port}/tcp"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def build_prefix(target_chars=12000):
    base = (
        "Consider the following reasoning trace. Goldbach's conjecture claims "
        "every even integer greater than two is the sum of two primes; while "
        "unproved, numerical verification holds to extraordinary bounds. The "
        "twin prime conjecture, in contrast, asserts infinitely many pairs "
        "(p, p+2) with both prime; Zhang's 2013 bound of seventy million, "
        "subsequently sharpened by Maynard and the Polymath project, showed "
        "infinitely many pairs within a finite gap, but the exact gap of two "
        "remains open. Moving to complexity theory, P versus NP asks whether "
        "efficient verification implies efficient search; most researchers "
        "believe the separation holds, yet natural proofs, relativization, "
        "and algebrization barriers rule out vast families of attacks. In "
        "measure theory, Lebesgue integration generalizes Riemann by first "
        "approximating the codomain rather than the domain, which handles "
        "pathological functions that Riemann cannot. The dominated "
        "convergence theorem, Fatou's lemma, and the monotone convergence "
        "theorem together furnish the core limit-exchange tools. In "
        "topology, the fundamental group of a space encodes loops up to "
        "homotopy, and van Kampen's theorem expresses the fundamental "
        "group of a union in terms of the groups of the parts. Simply "
        "connected spaces have trivial fundamental group; spheres of "
        "dimension two and higher qualify, while the circle does not. "
    )
    chunks = []
    total = 0
    while total < target_chars:
        chunks.append(base)
        total += len(base)
    return "".join(chunks)


PROMPT_PREFIX = build_prefix()


def create_sidecar():
    cmd = [
        sys.executable, ENCODER,
        "--out", SIDECAR_PATH,
        "--num-layers", str(SIDECAR_NUM_LAYERS),
        "--head-dim", str(SIDECAR_HEAD_DIM),
        "--num-heads", str(SIDECAR_NUM_HEADS),
        "--num-kv-heads", str(SIDECAR_NUM_KV_HEADS),
        "--bits-k", str(SIDECAR_BITS_K),
        "--bits-v", str(SIDECAR_BITS_V),
    ]
    subprocess.run(cmd, check=True)


def _terminate(proc, timeout=5):
    proc.terminate()
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def run_case(name, port, extra_args=None):
    extra_args = extra_args or []
    cleanup_port(port)
    cmd = [
        LLAMA_SERVER,
        "-m", MODEL_PATH,
        "--host", "127.0.0.1",
        "--port", str(port),
        "--parallel", "1",
        "--ctx-size", "16384",
    ] + extra_args
    print(f"Starting {name} server on port {port}...")

    stderr_path = os.path.join(TMP_DIR, f"{name}.stderr.log")
    stderr_file = open(stderr_path, "wb")
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=stderr_file)

    url = f"http://127.0.0.1:{port}"
    ready = False
    start_time = time.time()
    while time.time() - start_time < 120:
        try:
            resp = requests.get(f"{url}/health", timeout=1)
            if resp.status_code == 200:
                ready = True
                break
        except Exception:
            pass
        time.sleep(0.5)

    if not ready:
        print(f"Server {name} failed to become ready.")
        _terminate(proc, timeout=5)
        stderr_file.close()
        return None

    payload = {
        "model": "local/Qwen3.5-9B.Q5_K_M.gguf",
        "messages": [{
            "role": "user",
            "content": PROMPT_PREFIX + "\n\nContinue this sentence with around 20 plain words: The quick brown fox",
        }],
        "temperature": 0,
        "max_tokens": 24,
        "seed": 42,
        "logprobs": True,
        "top_logprobs": 5,
    }

    data = None
    try:
        resp = requests.post(f"{url}/v1/chat/completions", json=payload, timeout=300)
        data = resp.json()
        with open(os.path.join(TMP_DIR, f"{name}.resp.json"), "w") as f:
            json.dump(data, f)
    except Exception as e:
        print(f"Error during request for {name}: {e}")
    finally:
        _terminate(proc, timeout=10)
        stderr_file.close()

    with open(stderr_path, "r", errors="replace") as f:
        stderr_text = f.read()

    if data is None:
        return None
    return data, stderr_text


def get_metrics(data):
    if not data or "choices" not in data:
        return None
    choice = data["choices"][0]
    text = choice["message"]["content"]
    logprobs_data = choice.get("logprobs", {}).get("content", [])

    assert logprobs_data, "logprobs.content is empty; llama-server build lacks logprobs support"

    first_token = logprobs_data[0].get("token")
    first_logprob = logprobs_data[0].get("logprob")
    sum_lp = sum(lp.get("logprob", 0) for lp in logprobs_data)
    avg_lp = sum_lp / len(logprobs_data)
    ppl = math.exp(-avg_lp)

    return {
        "text": text,
        "first_token": first_token,
        "first_logprob": first_logprob,
        "avg_logprob": avg_lp,
        "ppl": ppl,
    }


def analyze(ref_data, tq_data):
    ref_m = get_metrics(ref_data)
    tq_m = get_metrics(tq_data)

    print("\nMetrics Report:")
    for label, m in [("Reference", ref_m), ("TurboQuant", tq_m)]:
        print(f"--- {label} ---")
        if m:
            print(f"First Token: {m['first_token']}")
            print(f"First Logprob: {m['first_logprob']}")
            print(f"Avg Logprob: {m['avg_logprob']}")
            print(f"Perplexity: {m['ppl']}")
        else:
            print("No data available.")

    if ref_m is None or tq_m is None:
        print("FAIL: missing metrics.")
        sys.exit(1)

    if ref_m["text"] != tq_m["text"]:
        print("FAIL: pass-through divergence in generated text.")
        print(f"  ref: {ref_m['text']!r}")
        print(f"  tq : {tq_m['text']!r}")
        sys.exit(1)

    if ref_m["first_logprob"] != tq_m["first_logprob"]:
        print(
            f"FAIL: pass-through divergence in first_logprob "
            f"({ref_m['first_logprob']} vs {tq_m['first_logprob']})."
        )
        sys.exit(1)

    if ref_m["avg_logprob"] != tq_m["avg_logprob"]:
        print(
            f"FAIL: pass-through divergence in avg_logprob "
            f"({ref_m['avg_logprob']} vs {tq_m['avg_logprob']})."
        )
        sys.exit(1)

    print("\nPASS: reference and turboquant match bit-for-bit in pass-through mode.")


create_sidecar()
ref_result = run_case("reference", 39101)
tq_result = run_case(
    "turboquant", 39102,
    ["--turboquant", SIDECAR_PATH, "--turboquant-bits-k", "8", "--turboquant-bits-v", "8"],
)

if ref_result is None or tq_result is None:
    print("FAIL: one or both servers failed to produce a response.")
    sys.exit(1)

ref_data, _ = ref_result
tq_data, tq_stderr = tq_result

if "turboquant: engaged" not in tq_stderr:
    print("FAIL: 'turboquant: engaged' not found in TurboQuant server stderr.")
    print("      Sidecar did not load, or the loader silently fell back.")
    sys.exit(1)

analyze(ref_data, tq_data)
