#!/usr/bin/env python3
"""Can model B run a Vector op while model A waits in Vector AllReduce?"""
import multiprocessing as mp
import os
import queue
import random
import time
TP = 8
def model_a(rank, port, start, called, returned, messages):
    try:
        os.environ.update(XLITE_NODE_IPS="127.0.0.1", XLITE_PORT=str(port), XLITE_DISABLE_XCCL="false")
        import torch
        import torch_npu  # noqa: F401
        from xlite._C import Runtime, all_reduce
        torch.npu.set_device(rank)
        runtime = Runtime(rank, 128, rank, TP, 1)
        src = torch.ones((16, 5120), dtype=torch.bfloat16, device=f"npu:{rank}")
        dst = torch.empty_like(src)
        torch.npu.synchronize()
        messages.put(("READY", rank, ""))
        start.wait()
        if rank < TP // 2:
            called.set()
            all_reduce(runtime, dst, src)
            torch.npu.synchronize()
            returned.set()
        else:
            time.sleep(3600)  # Deliberately withhold half of model A's ranks.
    except BaseException as error:
        messages.put(("ERROR", rank, repr(error)))
def model_b(start, messages):
    try:
        import torch
        import torch_npu  # noqa: F401
        torch.npu.set_device(0)
        x = torch.randn((64 * 524_288,), dtype=torch.float16, device="npu:0")
        torch.npu.synchronize()
        messages.put(("READY", 0, ""))
        start.wait()
        out = torch.sigmoid(x)
        torch.npu.synchronize()
        messages.put(("B_DONE", 0, ""))
    except BaseException as error:
        messages.put(("ERROR", 0, repr(error)))
def wait_for(messages, wanted, count, timeout):
    deadline, seen = time.monotonic() + timeout, 0
    while seen < count:
        try:
            kind, rank, detail = messages.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty:
            print(f"TIMEOUT waiting {wanted}: {seen}/{count}", flush=True); return False
        if kind == "ERROR":
            print(f"ERROR rank={rank}\n{detail}", flush=True)
            return None
        seen += kind == wanted
    return True
def main():
    ctx = mp.get_context("spawn")
    messages, returned = ctx.Queue(), ctx.Event()
    called = [ctx.Event() for _ in range(TP)]
    a_start, b_start, port = ctx.Event(), ctx.Event(), random.randint(20000, 30000)
    jobs = [ctx.Process(target=model_a, args=(rank, port, a_start, called[rank], returned, messages))
            for rank in range(TP)]
    jobs += [ctx.Process(target=model_b, args=(b_start, messages))]
    for job in jobs:
        job.start()
    try:
        if not wait_for(messages, "READY", TP + 1, 300):
            print("RESULT=SETUP_FAILED")
            return 2
        a_start.set()
        if not all(event.wait(30) for event in called[:TP // 2]):
            print("RESULT=SETUP_FAILED")
            return 2
        time.sleep(3)
        if returned.is_set():
            print("RESULT=ALLREDUCE_DID_NOT_BLOCK")
            return 2
        print("A is waiting in Vector AllReduce; starting B's Vector Sigmoid", flush=True)
        b_start.set()
        passed = wait_for(messages, "B_DONE", 1, 30)
        if passed is None: return 2
        if returned.is_set():
            print("RESULT=ALLREDUCE_DID_NOT_STAY_BLOCKED")
            return 2
        print("RESULT=" + ("PASS" if passed else "BLOCKED"), flush=True)
        return 0 if passed else 1
    finally:
        for job in jobs:
            job.terminate()
        for job in jobs:
            job.join(5)
if __name__ == "__main__":
    raise SystemExit(main())
