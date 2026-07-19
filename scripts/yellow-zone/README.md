# Yellow-zone deployment scripts

These scripts are the reviewable, version-controlled entry points for the
yellow-zone experiment. Run them from the repository root on the yellow-zone
Linux host.

## Stop all local vLLM processes

`stop-vllm-processes.sh` finds vLLM launchers, engine/worker processes, and all
of their descendants (including Python multiprocessing helpers). It first sends
`SIGTERM`, waits up to 10 seconds, and sends `SIGKILL` to anything still running.

```bash
bash scripts/yellow-zone/stop-vllm-processes.sh
```

Preview the matched processes without stopping them, or skip the grace period:

```bash
bash scripts/yellow-zone/stop-vllm-processes.sh --dry-run
bash scripts/yellow-zone/stop-vllm-processes.sh --force
```

## Native vLLM baseline

`run-native-vllm.sh` starts one Qwen3-8B instance on physical NPU 0 using the
official vLLM-Ascend image and fine-grained driver mounts. It deliberately does
not load XLite or vCANN-RT. The script waits for `/v1/models`, sends a minimal
completion request, prints recent logs, and leaves a successful container
running.

```bash
bash scripts/yellow-zone/run-native-vllm.sh
```

Defaults may be overridden without editing the script. For example:

```bash
DEVICE=/dev/davinci1 PORT=8001 CONTAINER_NAME=vllm-qwen3-8b-native-1 \
  bash scripts/yellow-zone/run-native-vllm.sh
```

The script only removes a pre-existing container whose name exactly matches
`CONTAINER_NAME`; it does not inspect or modify unrelated containers.

## Native Ascend diagnostics

`diagnose-native-ascend.sh` reproduces the native container's device and driver
mounts without loading a model. It reports host/container versions, runs
`npu-smi` inside the container, and separately tests torch-npu SoC discovery and
tensor allocation.

```bash
bash scripts/yellow-zone/diagnose-native-ascend.sh
```
