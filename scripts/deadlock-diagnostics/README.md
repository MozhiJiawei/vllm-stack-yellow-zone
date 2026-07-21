# vCANN-RT deadlock capture

This diagnostic path does not patch or import vLLM. A small optional probe in
`libvruntime.so` records kernel, event, and scheduler transitions in a fixed
in-memory ring. After a hang, the collector freezes processes that loaded
`libvruntime.so`, attaches GDB, and exports the ring and scheduler sync state.

## Build the diagnostic runtime

Build inside the same CANN environment used for the deployed runtime:

```bash
cd vcann-rt/ubs-virt-enpu/vcann-rt
ENABLE_DEADLOCK_DIAGNOSTICS=1 ./make_build.sh
```

This retains the normal `-O2` optimization, compiles in the probe, and adds
debugger symbols; it does not change the scheduling policy. A normal build
without `ENABLE_DEADLOCK_DIAGNOSTICS=1` contains neither the probe calls nor
the trace buffer. Install the resulting `build/libvruntime.so` in both workload
containers.

The diagnostic build still defaults to inactive. Enable the in-memory ring
before starting each workload:

```bash
export ENPU_DEADLOCK_TRACE=1
```

Do not enable `ENPU_LOG_LEVEL=4` for the primary reproduction. DEBUG logging is
per-task and can perturb a timing-sensitive hang. The ring performs no file
I/O or allocation in the task submission path. It uses a per-slot lock with no
global submission lock; contention is only possible after the 4096-record ring
wraps. The collector exports the full ring by default.

GDB must be installed in the workload image and the container must permit
ptrace. With containerd this normally requires the task to be created with
`CAP_SYS_PTRACE` and a seccomp policy that permits `ptrace`; verify those before
the reproduction.

## Verify target discovery

Copy `collect_vcann_deadlock.py` and `vcann_trace_gdb.py` into a container, then
run:

```bash
python3 /tmp/collect-vcann-deadlock.py preflight --expected-processes 8
```

`preflight` also reports the GDB path, helper presence, Yama ptrace scope, and
the collector process capability/seccomp status from `/proc/self/status`.

Automatic discovery requires both:

- `libvruntime.so` appears in `/proc/PID/maps`;
- the process has an open `/dev/davinciN` file descriptor.

If the process count is ambiguous, pass each workload PID explicitly with
repeated `--pid` arguments. The collector refuses a PID that has not loaded
`libvruntime.so` and validates its process start time before every signal.

## Capture two ctr containers

Run from the host after the deadlock is visible:

```bash
scripts/deadlock-diagnostics/collect_two_ctr_containers.sh \
  --namespace default \
  --container-a xlite-xccl-a \
  --container-b xlite-xccl-b \
  --run-id run-001 \
  --output /tmp/vcann-deadlock-run-001
```

Use `--pid-a 101,102,...` and `--pid-b 201,202,...` when automatic discovery is
not unique. By default all selected workload processes remain SIGSTOP-frozen.
Use `--resume-after` only when preserving the scene is not required.

The archive contains, for every process:

- all native thread backtraces;
- `g_vcann_sync_probe`, including vNPU, owner, turn, and synchronized stream;
- the newest trace records, with symbol/name resolution when available;
- `/proc` thread state and only diagnostic environment variables;
- vCANN log files whose filename contains the exact PID.

Trace entries are hook-entry attempts recorded before `core_limiter`; they do
not prove that the Runtime accepted or completed the operation. This placement
keeps the original `core_limiter`-to-Runtime call interval unchanged.
`vcann-summary.csv` highlights the synchronized stream, the last attempt before
the scheduler entered `rtStreamSynchronize`, and the first attempt after that
point. A post-sync attempt may still be waiting in `core_limiter`.

A kernel handle is opaque in some CANN launch APIs; in that case the trace only
provides its address. Name pointers are resolved by GDB on a best-effort basis
and may already be stale. Exact xLite collective generations still require an
xLite-side generation probe.
