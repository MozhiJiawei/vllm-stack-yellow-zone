# vLLM Stack Yellow Zone

Code baseline for the XLite + vCANN-RT two-card, two-Qwen3-8B experiment.

This baseline matches container image
`quay.io/ascend/vllm-ascend:v0.19.1rc1`:

- vLLM Ascend: `v0.19.1rc1`
- vLLM: `v0.19.1` (pinned by the vLLM Ascend release Dockerfile)
- vCANN-RT: local `master-rebase` snapshot

Clone with submodules:

```bash
git clone --recurse-submodules --branch vllm-ascend-v0.19.1rc1-yellow-zone \
  https://github.com/MozhiJiawei/vllm-stack-yellow-zone.git
```

Exact source revisions are recorded in `SOURCES.lock`.

Yellow-zone deployment entry points are kept under
[`scripts/yellow-zone`](scripts/yellow-zone/README.md). Keep executable logic in
those version-controlled scripts; Issue comments should only update the synced
checkout and invoke an entry point.
