# Two-container xLite/XCCL experiments

These entry points split model A and model B into separate vCANN containers.
Each container still owns and synchronizes its local workers with Python
`multiprocessing`. A lightweight coordinator on the host carries only phase
commands and worker status over TCP; it never imports torch, torch-npu, or
xLite and never joins an XCCL communicator.

- `repro-tp8-xlite-xccl-deadlock-two-container.py`
- `probe-xccl-hold-vs-vector-two-container.py`

The original single-container scripts are unchanged and remain the reference
for the NPU workloads.

## Network requirements

The coordinator listens on a host TCP port. Both containers need outbound TCP
access to that address, but do not need direct connectivity to each other.

- Docker Desktop containers can normally use `host.docker.internal`.
- On a Linux vCANN host, use a host IP reachable from both containers.
- Keep the port on a trusted test network. The protocol validates experiment,
  run ID, role, and protocol version, but provides no encryption or public
  network authentication.

The two participants may start before the coordinator; they retry until
`--connect-timeout` expires. Start all three commands with the same unique
`--run-id` and the same script revision. The coordinator is the authority for
the printed `RESULT` and exit code. It sends that result to both participants,
which clean up their workers and exit with the same code.

## TP8 native-forward deadlock reproducer

Run the coordinator on the host:

```bash
python scripts/repro-tp8-xlite-xccl-deadlock-two-container.py \
  --role coordinator \
  --run-id deadlock-001 \
  --listen-host 0.0.0.0 \
  --control-port 29680 \
  --schedule crossed
```

Run model A in vCANN container A:

```bash
python3 scripts/repro-tp8-xlite-xccl-deadlock-two-container.py \
  --role A \
  --run-id deadlock-001 \
  --coordinator HOST_IP:29680
```

Run model B in vCANN container B:

```bash
python3 scripts/repro-tp8-xlite-xccl-deadlock-two-container.py \
  --role B \
  --run-id deadlock-001 \
  --coordinator HOST_IP:29680
```

Replace `HOST_IP` with `host.docker.internal` under Docker Desktop or a
reachable host address on Linux. Run `--schedule aligned` first as the normal
control, then use a new run ID with `--schedule crossed` for the reproduction.
Batch, shape, dtype, scheduling, and timeout arguments belong on the
coordinator command; its `INIT` command sends one canonical configuration to
both containers.

Each participant automatically selects a free local xLite base port, including
the required `port+400` companion. If the containers share the host network,
pass distinct participant-local ports explicitly, for example
`--xlite-port 23000` for A and `--xlite-port 24000` for B.

## XCCL hold-vs-vector probe

Run the coordinator on the host:

```bash
python scripts/probe-xccl-hold-vs-vector-two-container.py \
  --role coordinator \
  --run-id vector-001 \
  --listen-host 0.0.0.0 \
  --control-port 29681
```

Run the XCCL workload in vCANN container A:

```bash
python3 scripts/probe-xccl-hold-vs-vector-two-container.py \
  --role A \
  --run-id vector-001 \
  --coordinator HOST_IP:29681
```

Run the independent Vector Sigmoid in vCANN container B:

```bash
python3 scripts/probe-xccl-hold-vs-vector-two-container.py \
  --role B \
  --run-id vector-001 \
  --coordinator HOST_IP:29681
```

The coordinator waits until A ranks 0-3 have entered AllReduce, confirms that
the collective remains blocked, and only then starts B's Vector Sigmoid. A
`PASS` means B finished while A remained blocked; `BLOCKED` means B did not
finish within `--vector-timeout`.

## Result codes

The scripts retain the single-container result convention:

- `0`: the selected control or expected reproduction/probe result succeeded.
- `1`: the experiment completed with the opposite outcome (for example no
  deadlock, an aligned hang, or a blocked Vector operation).
- `2`: setup failure, runtime error, invalid progress state, protocol failure,
  or an inconclusive hang.
