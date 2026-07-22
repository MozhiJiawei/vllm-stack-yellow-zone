# vCANN-RT deadlock capture

This diagnostic path is implemented in `libvruntime.so`; it does not patch or
import vLLM.  An opt-in fixed-memory probe records Runtime submissions,
synchronization state, and the kernel names registered by xLite.  After a hang,
the collector freezes the workload, attaches GDB, and exports native stacks and
the probe ABI.

## Build and enable

Build in the same CANN environment as the deployed runtime:

```bash
cd vcann-rt/ubs-virt-enpu/vcann-rt
ENABLE_DEADLOCK_DIAGNOSTICS=1 ./make_build.sh
```

Install `build/libvruntime.so` in the workload containers, then set the switch
before starting the workload:

```bash
export ENPU_DEADLOCK_TRACE=1
```

Without the CMake option, the probe code and symbols are absent.  With the
option but without the environment variable, the hot-path macros only perform
one unlikely enable check.  The enabled probe remains `-O2`, performs no file
I/O or allocation on submission, and uses a 4096-entry per-process trace plus a
512-entry fixed kernel registry.  It does not change the scheduling policy.

Do not enable `ENPU_LOG_LEVEL=4` for the primary reproduction because per-task
DEBUG logging can perturb timing.  GDB must be present and the container must
allow `ptrace` (normally `CAP_SYS_PTRACE` plus a permitting seccomp policy).

## What is captured

The diagnostic build intercepts xLite's registration and launch path:

- `aclrtBinaryGetFunction` and `rtFunctionRegister` copy registered kernel
  names into stable probe storage. The ACL hook maps the returned function
  handle used later by `rtLaunchKernelByFuncHandle*` without dereferencing an
  opaque Runtime object;
- launch records retain stream, handle, tiling key, block count, and argument
  metadata without dereferencing opaque Runtime objects;
- application `rtDeviceSynchronize*`/`rtStreamSynchronize` calls and vCANN's
  scheduler `rtStreamSynchronize` are recorded as begin/end pairs;
- the collector reconstructs the native Qwen3 dense layer phases from the
  registered xLite names.  A completed device sync (the reproducer warm-up) is
  the exact layer-0 sequence anchor.

The output distinguishes evidence from inference.  A launch record proves the
host called Runtime, not that the device completed the kernel.  A blocked
scheduler sync bounds the unresolved ordered window after the preceding
successful scheduler sync; `blocked_kernel` is the first candidate in that
window and `completion_evidence` states this limitation.  A blocked device sync
has no per-stream completion boundary, so its reported candidate is explicitly
labelled “last submitted only”.  Native stacks, the candidate window, kernel
names, Qwen layer/phase, and XCCL/vCANN logs must be considered together.

## Preflight and capture

Copy `collect_vcann_deadlock.py` and `vcann_trace_gdb.py` into the workload
container.  Verify discovery before reproducing:

```bash
python3 /tmp/collect-vcann-deadlock.py preflight --expected-processes 8
```

Automatic discovery requires both `libvruntime.so` in `/proc/PID/maps` and an
open `/dev/davinciN` descriptor.  Repeated `--pid` arguments can resolve an
ambiguous process set.  The collector validates process start times before
every signal.

Capture one container after the stall is visible:

```bash
python3 /tmp/collect-vcann-deadlock.py capture \
  --model A \
  --expected-processes 8 \
  --qwen-layers 64 \
  --output /tmp/vcann-capture-A
```

By default the selected workers remain `SIGSTOP`-frozen.  Resume only after the
capture is preserved:

```bash
python3 /tmp/collect-vcann-deadlock.py resume --capture-dir /tmp/vcann-capture-A
```

For the existing two-`ctr` deployment, capture both sides from the host:

```bash
scripts/deadlock-diagnostics/collect_two_ctr_containers.sh \
  --namespace default \
  --container-a xlite-xccl-a \
  --container-b xlite-xccl-b \
  --run-id run-001 \
  --output /tmp/vcann-deadlock-run-001
```

GDB Python support is optional.  When it is unavailable, GDB dumps the four
fixed ABI objects (trace, scheduler probe, host-sync probe, and kernel
registry), and the container's normal Python decodes them.

## Minimal real-NPU verification

`verify_xlite_qwen_trace.py` uses the same native xLite model builder as the
Qwen3-32B TP8 reproducer.  It runs one layer: all ranks complete a warm-up, then
only rank 0 submits the diagnostic forward while ranks 1-7 deliberately withhold
theirs.  This creates a bounded capture window; it is not evidence of a natural
deadlock.

```bash
export ENPU_DEADLOCK_TRACE=1
python3 scripts/deadlock-diagnostics/verify_xlite_qwen_trace.py \
  --tp-size 8 \
  --hold-seconds 600
```

After `CAPTURE_READY`, use a second shell in the same container:

```bash
python3 scripts/deadlock-diagnostics/collect_vcann_deadlock.py capture \
  --model QWEN-TRACE-VERIFY \
  --expected-processes 8 \
  --qwen-layers 1 \
  --output /tmp/qwen-trace-verify
```

A successful feature check has eight enabled traces.  Rank 0 must contain
non-empty `kernel_names` including Qwen matmul, attention, and all-reduce names;
its records must carry `qwen_layer: 0` and phases such as
`ATTENTION_QKV`, `ATTENTION_TP_ALLREDUCE`, and `MLP_TP_ALLREDUCE`.  The sync
probe should show the intentional wait.  Resume the capture or stop the verifier
after inspecting the JSON; resuming cannot satisfy the deliberately missing
collective ranks.

For temporary hook-path debugging, also set `ENPU_DEADLOCK_TRACE_LOG=1`.
Diagnostic builds then emit `DEADLOCK_TRACE_REGISTER` and
`DEADLOCK_TRACE_LAUNCH` at ERROR level. This is intentionally noisy and remains
off by default; use it only for a bounded reproducer run.

For a container started with four NPUs, use `--tp-size 4` and pass
`--expected-processes 4` to the collector. Device ordinals inside the container
remain the dense local range `0..3`; the host-side physical IDs are selected by
the container restart script.
