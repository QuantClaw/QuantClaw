import os
import struct
import subprocess
import time
import json
import requests
import signal
import math

MODEL_PATH = "/home/jmazz/Projects/QuantClaw/providers.local/qwen3.5-9B-claude4.6-distillation/Qwen3.5-9B.Q5_K_M.gguf"
SIDECAR_PATH = "/home/jmazz/Projects/QuantClaw/tmp/Qwen3.5-9B.Q5_K_M.turboquant.bin"
LLAMA_SERVER = "/home/jmazz/Projects/QuantClaw/build-cmake43/bin/llama-server"
TMP_DIR = "/home/jmazz/Projects/QuantClaw/tmp"

os.makedirs(TMP_DIR, exist_ok=True)

def cleanup_llama():
    subprocess.run(["pkill", "-f", "llama-server"], stderr=subprocess.DEVNULL)

def create_sidecar():
    # Schema per docs/BRIDGE.md §2.2 and ui/llama.cpp/llama.cpp/src/turboquant.cpp loader.
    # Minimal valid fixture: hooks are pass-through scaffolding, so values are unused.
    version = 1
    num_layers = 1
    head_dim = 4
    num_heads = 1
    num_kv_heads = 1
    bits_k = 8
    bits_v = 8

    header = struct.pack(
        "<4sIIIIIBB6x",
        b"TQNT",
        version,
        num_layers,
        head_dim,
        num_heads,
        num_kv_heads,
        bits_k,
        bits_v,
    )
    assert len(header) == 32

    per_layer_f32 = 2 * head_dim * head_dim + ((1 << bits_k) + (1 << bits_v)) * head_dim
    payload = b"\0" * (num_layers * per_layer_f32 * 4)

    print(f"Creating sidecar at {SIDECAR_PATH} ({len(header) + len(payload)} bytes)...")
    with open(SIDECAR_PATH, "wb") as f:
        f.write(header)
        f.write(payload)

def run_case(name, port, extra_args=[]):
    cleanup_llama()
    cmd = [LLAMA_SERVER, "-m", MODEL_PATH, "--host", "127.0.0.1", "--port", str(port), "--parallel", "1", "--ctx-size", "4096"] + extra_args
    print(f"Starting {name} server on port {port}...")
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    url = f"http://127.0.0.1:{port}"
    ready = False
    start_time = time.time()
    while time.time() - start_time < 60:
        try:
            resp = requests.get(f"{url}/health", timeout=1)
            if resp.status_code == 200:
                ready = True
                break
        except:
            pass
    
    if not ready:
        print(f"Server {name} failed to become ready.")
        proc.terminate()
        return None

    payload = {
        "model": "local/Qwen3.5-9B.Q5_K_M.gguf",
        "messages": [{"role": "user", "content": "Continue this sentence with around 20 plain words: The quick brown fox"}],
        "temperature": 0,
        "max_tokens": 24,
        "seed": 42,
        "logprobs": True,
        "top_logprobs": 5
    }
    
    try:
        resp = requests.post(f"{url}/v1/chat/completions", json=payload, timeout=60)
        data = resp.json()
        with open(os.path.join(TMP_DIR, f"{name}.resp.json"), "w") as f:
            json.dump(data, f)
        return data
    except Exception as e:
        print(f"Error during request for {name}: {e}")
        return None
    finally:
        proc.terminate()
        proc.wait()

def analyze(ref_data, tq_data):
    def get_metrics(data):
        if not data or "choices" not in data:
            return None
        choice = data["choices"][0]
        text = choice["message"]["content"]
        logprobs_data = choice.get("logprobs", {}).get("content", [])
        
        if not logprobs_data:
            return {"text": text, "first_token": None, "first_logprob": None, "avg_logprob": None, "ppl": None}
        
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
            "ppl": ppl
        }

    ref_m = get_metrics(ref_data)
    tq_m = get_metrics(tq_data)
    
    print("\nMetrics Report:")
    for name, m in [("Reference", ref_m), ("TurboQuant", tq_m)]:
        print(f"--- {name} ---")
        if m:
            print(f"First Token: {m['first_token']}")
            print(f"First Logprob: {m['first_logprob']}")
            print(f"Avg Logprob: {m['avg_logprob']}")
            print(f"Perplexity: {m['ppl']}")
        else:
            print("No data available.")

    if ref_m and tq_m:
        print(f"\nText Equality: {ref_m['text'] == tq_m['text']}")
        if "error" in ref_data: print(f"Ref Error: {ref_data['error']}")
        if "error" in tq_data: print(f"TQ Error: {tq_data['error']}")

create_sidecar()
ref = run_case("reference", 39101)
tq = run_case("turboquant", 39102, ["--turboquant", SIDECAR_PATH, "--turboquant-bits-k", "8", "--turboquant-bits-v", "8"])
analyze(ref, tq)
