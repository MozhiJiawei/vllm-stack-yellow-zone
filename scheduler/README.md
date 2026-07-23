# vLLM pair scheduler

`vllm-pair-scheduler` serializes the `model_forward` sections of two vLLM
instances on the same Linux host. Protocol v3 gates `WorkerProc.execute_model`
with a shared `mmap`, lock-free C11 atomics, and futex wakeups. It does not
communicate with vCANN.

The primary is a control-plane role, not an active/passive serving role. Both
instances may execute requests, but only the primary coordinator grants the
single forward lease. There is no automatic promotion.

## Configuration

| Variable | Values / default |
| --- | --- |
| `VLLM_PAIR_SCHED_MODE` | `off` (default), `elastic` |
| `VLLM_PAIR_SCHED_ROLE` | `primary`, `standby` |
| `VLLM_PAIR_SCHED_INSTANCE_ID` | `A` for primary, `B` for standby |
| `VLLM_PAIR_SCHED_PAIR_ID` | Required stable pair name |
| `VLLM_PAIR_SCHED_SHM_DIR` | `/dev/shm/vllm-pair-scheduler` |
| `VLLM_PAIR_SCHED_INIT_TIMEOUT_MS` | `30000` |
| `VLLM_PAIR_SCHED_FORWARD_TIMEOUT_MS` | `30000` |
| `VLLM_PAIR_SCHED_HEARTBEAT_MS` | `100` |
| `VLLM_PAIR_SCHED_PEER_TIMEOUT_MS` | `1000` |

Both containers must mount the same host directory at the configured shared
memory path and run with a common uid/gid that can read and write mode `0660`
files. `fix-shared`, Pipeline Parallel, multi-node TP, automatic failover, and
more than 64 local workers are deliberately unsupported.

Each local TP worker sets one READY bit before the instance receives a single
grant. The grant is released only after all local workers set COMPLETE. TP4 is
therefore one grant with four READY and four COMPLETE bits, not four or eight
grants. The completion contract is the host return of every worker's
`execute_model` call; no device event is added.

Sampling and all other RPC methods bypass the gate. EngineCore,
AsyncScheduler, FutureWrapper, placeholders, and the batch queue are unchanged,
so sampling futures may remain queued while the next batch is scheduled.

## Inspection

Inspection is read-only and does not register another participant:

```bash
vllm-pair-scheduler-inspect \
  --pair-id qwen3-4b-tp4-npu4-7 \
  --shm-dir /dev/shm/vllm-pair-scheduler \
  --json
```

Exit status is `0` for RUNNING, `2` for FAILED, `3` for SHUTDOWN or a stale
primary heartbeat, and `1` when the generation cannot be read. A forward
deadline marks the pair FAILED but deliberately does not kill a blocked
worker; an external supervisor must stop and restart both instances. The JSON
includes `instances.A/B.registration_complete`, aggregate masks, and each
worker's pid, session, heartbeat, sequence, READY and COMPLETE state.

## Benchmark

The handoff benchmark records timestamps in process memory and performs no file
I/O on its hot path:

```bash
python scheduler/benchmarks/handoff.py \
  --iterations 1000000 \
  --workers 4 \
  --cpu-a 2 \
  --cpu-b 8 \
  --timeout-seconds 1800 \
  --max-p99-us 150
```
