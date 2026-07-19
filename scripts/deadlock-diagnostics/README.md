# vLLM Ascend deadlock snapshots

These tools collect Python, Linux thread, and native stacks from two colocated
TP8 xLite services. They do not start services or workloads.

## Patch an installed vLLM Ascend wheel

The source change is also provided as
`patches/vllm-ascend-v0.19.1rc1-deadlock-diagnostics.patch`. Apply it to the
installed package with the helper, then restart both services:

```bash
bash scripts/deadlock-diagnostics/apply_site_packages_patch.sh \
  --patch patches/vllm-ascend-v0.19.1rc1-deadlock-diagnostics.patch
```

The helper discovers the active `site-packages` directory with Python, checks
the installed version, and runs `patch --dry-run` before changing files. To
restore the wheel files, run the same command with `--reverse` and restart the
services again.

## Capture a scene

Enable diagnostics before starting each service. Use a common run ID and a
different model ID in each container:

```bash
export VLLM_ASCEND_DEADLOCK_DIAG=1
export VLLM_ASCEND_DEADLOCK_MODEL_ID=A  # B in the other container
export VLLM_ASCEND_DEADLOCK_RUN_ID=run-001
export VLLM_ASCEND_DEADLOCK_DIR=/tmp/vllm-deadlock-diag
```

After confirming the hang, collect both containers from the host:

```bash
scripts/deadlock-diagnostics/collect_two_containers.sh \
  --container-a model-a --container-b model-b \
  --run-id run-001 --output /tmp/deadlock-run-001
```

The default capture sends SIGUSR1 at 0, 100, 500, and 2000 ms, then SIGSTOPs
only the workers registered for the requested run. PID start times are checked
before every signal. The workers remain frozen after collection.

To inspect or resume one container:

```bash
python3 /tmp/vllm-deadlock-snapshot.py status \
  --run-id run-001 --model A
python3 /tmp/vllm-deadlock-snapshot.py resume \
  --run-id run-001 --model A
```

Run `preflight` before the workload when ptrace availability is uncertain.
Native stack failures are recorded without preventing Python and `/proc`
collection.

For a second reproduction, trace only representative ranks selected from the
first summary. For example:

```bash
python3 /tmp/vllm-deadlock-snapshot.py trace \
  --run-id run-001 --model A --rank 0 --rank 4 \
  --duration 2 --output /tmp/deadlock-run-001-strace-A
```

The trace is limited to synchronization, runtime ioctl, polling, IPC, and
read/write syscalls. It does not run during the default 16-rank capture.
