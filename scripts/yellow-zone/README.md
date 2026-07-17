# Yellow-zone deployment scripts

These scripts are the reviewable, version-controlled entry points for the
yellow-zone experiment. Run them from the repository root on the yellow-zone
Linux host.

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
