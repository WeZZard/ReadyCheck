#!/usr/bin/env python3
import argparse
import os
import selectors
import subprocess
import sys
import time


def stream_process(cmd, timeout_sec, cwd=None, env=None):
    sel = selectors.DefaultSelector()
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
        text=True,
        universal_newlines=True,
    )
    start = time.time()
    stdout_buf = []
    stderr_buf = []
    if proc.stdout:
        sel.register(proc.stdout, selectors.EVENT_READ, data=(stdout_buf, sys.stdout))
    if proc.stderr:
        sel.register(proc.stderr, selectors.EVENT_READ, data=(stderr_buf, sys.stderr))

    exit_code = None
    try:
        while True:
            if timeout_sec is not None and (time.time() - start) > timeout_sec:
                raise TimeoutError

            events = sel.select(timeout=0.1)
            for key, _ in events:
                buf, out = key.data
                chunk = key.fileobj.readline()
                if chunk:
                    buf.append(chunk)
                    try:
                        out.write(chunk)
                        out.flush()
                    except Exception:
                        pass
                else:
                    sel.unregister(key.fileobj)

            exit_code = proc.poll()
            if exit_code is not None and not events:
                break
        return exit_code, "".join(stdout_buf), "".join(stderr_buf)
    except TimeoutError:
        try:
            proc.kill()
        except Exception:
            pass
        return 124, "".join(stdout_buf), "".join(stderr_buf)


def main():
    ap = argparse.ArgumentParser(description="Run a command with a timeout, streaming stdout/stderr.")
    ap.add_argument("timeout", type=int, help="Timeout in seconds")
    ap.add_argument("cmd", nargs=argparse.REMAINDER, help="Command to run")
    args = ap.parse_args()

    if not args.cmd:
        print("No command provided", file=sys.stderr)
        return 2

    code, out, err = stream_process(args.cmd, args.timeout)
    if code == 124:
        print("\n--- TIMEOUT ---", file=sys.stderr)
        print(f"Command: {' '.join(args.cmd)}", file=sys.stderr)
        print(f"Collected stdout bytes: {len(out)}", file=sys.stderr)
        print(f"Collected stderr bytes: {len(err)}", file=sys.stderr)
    return 0 if code == 0 else (code if code is not None else 1)


if __name__ == "__main__":
    sys.exit(main())

