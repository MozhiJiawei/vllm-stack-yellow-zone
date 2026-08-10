# AllReduce + X operator catalog

This catalog targets the fixed Ascend environment used by the XCCL deadlock
investigation.  Model A is always an incomplete xLite TP8 AllReduce with only
rank 0 submitting.  Model X is the only configured variable.

## Source of the limits

The runtime source of truth is the installed CANN 8.5.1 tree in the container:

- `aarch64-linux/data/platform_config/Ascend910B4.ini` reports 20 Cube cores,
  40 Vector cores, 196608 bytes of UB, 32-byte UB blocks, 16x16x16 default
  Cube M/N/K granularity, and BF16 support.
- `opp/built-in/op_impl/ai_core/tbe/config/ascend910b/` supplies the installed
  operator metadata for math, NN, CV, and transformer operators.
- vLLM Ascend's `ApplyTopKTopPCustom` definition limits values and `p` to
  FP32/FP16/BF16, requires `k` to be INT32, and requires a two-dimensional
  values tensor.  Its tiling implementation uses `K_VALUE_MAX=1024`.

The `full_core` profiles are representative contention inputs, not performance
benchmarks.  Vector profiles expose at least 40 row, batch, or sequence work
partitions.  Cube dimensions are multiples of 16 and expose at least 20 output
tiles.  Every expanded case is checked against the configured memory ceiling.

## Usage

Validate and list the catalog locally:

```bash
python scripts/run-xccl-allreduce-x-matrix.py \
  --config configs/xccl-allreduce-x/cann-8.5.1-ascend910b4.yaml \
  --dry-run
```

On the Ascend host, use the namespace-preserving launcher.  Do not replace it
with `ctr tasks exec`; the Ascend driver can assign an incomplete device set to
the new exec process.

```bash
scripts/run-xccl-allreduce-x-on-ascend.sh \
  --phase preflight \
  --output /tmp/xccl-x-preflight.jsonl
```

Run `--phase all` to preflight each case and run contention only for preflight
passes.  `--scenario` and `--kind` may be repeated or combined to narrow a run.
