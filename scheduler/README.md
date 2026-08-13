# vLLM pair scheduler

This package serializes the execution rounds of two same-host vLLM instances.
Protocol v4 uses shared memory, C11 atomics, futexes, and one READY and COMPLETE
bit per local TP worker. A non-empty generation round holds its grant from
`execute_model` entry through the matching `sample_tokens` return. An
`execute_model` call that returns a result directly completes at that boundary,
and a standalone `sample_tokens` call acquires its own grant. Other RPCs bypass
the gate.

## Install

Run once inside each stopped vLLM container. Install the primary first:

```bash
bash /root/l00933108/scheduler/install-pair-scheduler.sh \
  /root/l00933108 primary
```

Then install the standby:

```bash
bash /root/l00933108/scheduler/install-pair-scheduler.sh \
  /root/l00933108 standby
```

The source root must contain `scheduler/` and
`patches/vllm-pair-elastic-scheduling.patch`. The vLLM source defaults to
`/vllm-workspace/vllm`; pass a third argument only when it lives elsewhere.

Both containers must mount the same host directory at
`/dev/shm/vllm-pair-scheduler`. The standby installer refuses to finish unless
it sees the primary's marker through that mount.

The installer builds and installs the wheel, applies the vLLM v0.19.1 patch,
creates the shared-memory directory, and writes the role. Start `vllm serve`
normally afterward. No `VLLM_PAIR_SCHED_*` environment variables are used.

The fixed first-version profile is:

- primary is instance A; standby is instance B;
- pair ID is `default`;
- shared memory is `/dev/shm/vllm-pair-scheduler`;
- initialization/execution-round/heartbeat/peer timeouts are
  30 s/30 s/100 ms/1 s. The existing `forward_timeout_ms` field names the
  execution-round deadline for compatibility.

Deleting `/etc/vllm-pair-scheduler/role` and restarting vLLM disables the
integration completely: the patched executor does not import the package or
replace `execute_model`.

For short diagnostic runs, set
`VLLM_PAIR_SCHED_DIAGNOSTICS_INTERVAL=128` before starting both instances.
This records sampling phase durations in memory and emits one
`PAIR_SCHED_SAMPLE_DIAG` summary per worker every 128 sampling calls. It is
disabled by default and does not wrap the sampling path unless explicitly set.
Do not enable the per-round JSONL trace for performance measurements because
its synchronous writes perturb TP-worker timing.

## Inspect

```bash
vllm-pair-scheduler-inspect --json
```

Exit status is `0` for RUNNING, `2` for FAILED, `3` for SHUTDOWN/stale, and
`1` for a read error. A failed pair must be stopped and restarted together.

The current version supports TP and EP, with at most 64 same-host local
workers. DP support is still under development. The two colocated models must
use the same communication domain. The scheduler adds no vLLM serve arguments;
existing model launch arguments remain unchanged.

## Yellow-zone native deployment

The yellow-zone preparation entry point creates two native Ascend containers,
installs xLite and the scheduler integration, and runs the scheduler preflight:

```bash
bash /root/l00933108/vllm-stack-yellow-zone/scripts/pair-scheduler/prepare-yellow-zone.sh
```

The defaults use:

- image `quay.io/ascend/vllm-ascend:v0.19.1rc1`;
- xLite wheel
  `/root/l00933108/deps/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl`;
- unrestricted private `ctr` client
  `/root/l00933108/.tools/containerd/bin/ctr`;
- models under `/cache/models`;
- physical NPUs `4,5,6,7` in both native containers.

Preflight traces are written outside the Git worktree under
`/root/l00933108/.artifacts/pair-scheduler` so repository synchronization is
not blocked by generated files.

This path does not build, mount, preload, or configure vCANN-RT. It also does
not require GDB, `enpu-monitor`, `npu_info.config`, or a custom
`ld.so.preload`. The legacy vCANN diagnostic scripts remain separate and are
not called by the pair-scheduler preparation flow.

After preparation succeeds, start the real TP4 pair in primary-first order:

```bash
bash /root/l00933108/vllm-stack-yellow-zone/scripts/pair-scheduler/start-yellow-zone.sh
```

PP, automatic primary promotion, fixed compute ratios, multi-node TP, and
device-side completion events are not supported. Protocol v4 closes the Host
boundary through Sampler return; asynchronous device work after that return is
still outside the completion contract.
