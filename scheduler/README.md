# vLLM pair scheduler

This package serializes the `execute_model` sections of two same-host vLLM
instances. Protocol v3 uses shared memory, C11 atomics, futexes, and one READY
and COMPLETE bit per local TP worker. Sampling and every other RPC bypass the
gate.

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
- initialization/forward/heartbeat/peer timeouts are 30 s/30 s/100 ms/1 s.

Deleting `/etc/vllm-pair-scheduler/role` and restarting vLLM disables the
integration completely: the patched executor does not import the package or
replace `execute_model`.

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

PP, automatic primary promotion, fixed compute ratios, multi-node TP, and
device-side completion events are not supported.
