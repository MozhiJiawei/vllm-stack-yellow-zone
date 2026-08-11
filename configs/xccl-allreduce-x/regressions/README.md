# Stable blocking regressions

Every YAML file in this directory is an independently runnable reproduction.
It contains one exact X configuration with `profile: regression` and declares
both expected phases:

- `preflight: PASS` proves that X completes without the incomplete AllReduce.
- `contention: BLOCKED_BY_A_ALLREDUCE` proves that the same X invocation does
  not complete during the configured device synchronization window after A
  enters the incomplete AllReduce.

Regression configurations default to three repetitions.  The runner exits
nonzero if any observed result differs from the declared expectation.

Current stable regressions:

- `custom-top-k-top-p-fp32-b4-v151936.yaml`
- `ffn-bf16-t2048-h5120-i6400-gelu.yaml`
- `reduce-sum-fp32-r1-c151936.yaml`
- `reduce-sum-fp32-r4-c151936.yaml`

Run a regression through the namespace-preserving Ascend launcher, for example:

```bash
./scripts/run-xccl-allreduce-x-on-ascend.sh \
  --config configs/xccl-allreduce-x/regressions/custom-top-k-top-p-fp32-b4-v151936.yaml \
  --phase all \
  --output /tmp/xccl-topk-topp-block-anchor.jsonl
```
