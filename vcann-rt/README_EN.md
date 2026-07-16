# ubs-virt

## Overview

ubs-virt supports virtualization and pooling, live migration policy decision, fast recovery and disaster recovery, and fast communication between VMs and containers, significantly improving virtualization performance.

## Components

1. ubs-virt-ovs: ubs-virt provides network capabilities and allows bandwidth configuration for specified URMA devices.
2. ubs-virt-enpu: ubs-virt enables NPU compute capability allocation, supporting configurable allocation of compute capability and graphics memory resources.
3. virt-awaresched: virt-awaresched optimizes virtualization scheduling, reducing the performance overhead of unnecessary vCPU migration and improving the linearity of VMs.

## Instruction

1. ubs-virt-ovs: [ubs-virt-ovs Instruction](./ubs-virt-ovs/README.md)
2. ubs-virt-enpu: [ubs-virt-enpu Instruction](./ubs-virt-enpu/README.md)
3. virt-awaresched: [virt-awaresched Instruction](./virt-awaresched/README.md)
