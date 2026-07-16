# ubs-virt

## 介绍

Ubs-virt supports virtualization and pooling, live migration policy decision, fast recovery and disaester recovery, and fast communication between VMs and containers, significantly improving virtualization performance.

## 涉及组件

1. ubs-virt-ovs: ubs-virt提供网络的能力的服务，支持配置指定urma设备的带宽。
2. ubs-virt-enpu: ubs-virt提供NPU算力切分的服务，支持配置指定的算力和显存资源。
3. virt-awaresched: virt-awaresched提供对虚拟化调度调优的服务，减少vCPU无意义迁移的性能开销，提升虚机的线性度。

## 使用说明

1. ubs-virt-ovs: [ubs-virt-ovs使用说明](./ubs-virt-ovs/README.md)
2. ubs-virt-enpu: [ubs-virt-enpu使用说明](./ubs-virt-enpu/README.md)
3. virt-awaresched：[virt-awaresched使用说明](./virt-awaresched/README.md)
