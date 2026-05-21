import os
import signal
import subprocess
import sys
import time
from importlib.util import find_spec
from pathlib import Path


ROOT = Path(__file__).resolve().parent

PROCESSES = [
    (
        "diagnostic-backend",
        [sys.executable, str(ROOT / "src" / "pc_backend" / "server.py")],
        {
            "PYTHONUNBUFFERED": "1",
        },
    ),
    (
        "control-backend",
        [sys.executable, str(ROOT / "pc_control_backend.py")],
        {
            "PYTHONUNBUFFERED": "1",
        },
    ),
    (
        "react-dev",
        ["npm.cmd", "run", "dev", "--", "--host", "0.0.0.0"],
        {
            "VITE_GATEWAY_BASE_URL": "http://127.0.0.1:5000",
        },
    ),
]


def check_prerequisites():
    missing_python = [
        module
        for module in ("flask", "doipclient", "udsoncan")
        if find_spec(module) is None
    ]
    missing_node = not (ROOT / "node_modules" / ".bin" / "vite").exists()

    if not missing_python and not missing_node:
        return True

    print("[start_pc] missing dependencies", flush=True)

    if missing_python:
        print(
            "[start_pc] missing Python modules: "
            + ", ".join(missing_python),
            flush=True,
        )
        print(
            "[start_pc] install with: "
            f"{sys.executable} -m pip install -r src/pc_backend/requirements.txt",
            flush=True,
        )

    if missing_node:
        print("[start_pc] missing Node dependencies: node_modules/.bin/vite", flush=True)
        print("[start_pc] install with: npm install", flush=True)

    return False


def start_process(name, command, extra_env):
    env = os.environ.copy()
    env.update(extra_env)

    print(f"[start_pc] starting {name}: {' '.join(command)}", flush=True)
    return subprocess.Popen(command, cwd=ROOT, env=env)


def stop_processes(processes):
    for name, process in processes:
        if process.poll() is None:
            print(f"[start_pc] stopping {name}", flush=True)
            process.terminate()

    deadline = time.time() + 5
    for name, process in processes:
        if process.poll() is None:
            remaining = max(0.1, deadline - time.time())
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                print(f"[start_pc] killing {name}", flush=True)
                process.kill()


def main():
    processes = []

    if not check_prerequisites():
        return 1

    def handle_signal(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, handle_signal)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, handle_signal)

    try:
        for name, command, extra_env in PROCESSES:
            processes.append((name, start_process(name, command, extra_env)))
            time.sleep(0.8)
            code = processes[-1][1].poll()
            if code is not None:
                raise RuntimeError(f"{name} exited with code {code}")

        print()
        print("[start_pc] all services started", flush=True)
        print("[start_pc] React: http://127.0.0.1:5173", flush=True)
        print("[start_pc] Diagnostics API: http://127.0.0.1:5000", flush=True)
        print("[start_pc] Control API: http://127.0.0.1:5100", flush=True)
        print("[start_pc] press Ctrl+C to stop all services", flush=True)

        while True:
            for name, process in processes:
                code = process.poll()
                if code is not None:
                    raise RuntimeError(f"{name} exited with code {code}")
            time.sleep(1)

    except KeyboardInterrupt:
        print("\n[start_pc] shutdown requested", flush=True)
    except Exception as exc:
        print(f"\n[start_pc] error: {exc}", flush=True)
        return 1
    finally:
        stop_processes(processes)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
