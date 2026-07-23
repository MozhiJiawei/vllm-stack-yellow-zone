# 黄区 Qwen3-4B TP4 双实例 elastic 调度远端状态

更新时间：2026-07-23
权威调测 Issue：<https://github.com/MozhiJiawei/vllm-stack-yellow-zone/issues/11>

> **状态更新：本文原有 `d06d6a7` READY/IDLE 调度断点已废止。**
> 下文保留的旧 patch、`rtDetSched*`、`shim4-7` 调度流程只用于历史
> 回溯，不得继续执行。当前入口是上层共享内存 elastic scheduler；
> vCANN 不再参与模型间 forward 仲裁。

## 用途

本文保留已经确认可复用的黄区硬件、容器和模型信息，并记录新上层调度
方案的部署断点。不要回到旧 Issue #7 的 ABI3 诊断流程，不要手写容器
创建命令，也不要继续应用旧 vLLM/vLLM-Ascend 协同调度 patch。

## 当前新断点：Worker 边界共享内存 elastic v3

协议已升级为 v3，旧 v2 generation 会被拒绝。调度点不再位于
EngineCore：每个 `WorkerProc` 仅在非空 `execute_model` 调用前后进入
共享内存 gate；EngineCore、AsyncScheduler、batch queue、placeholder
和 FutureWrapper 保持 vLLM v0.19.1 原生控制流。TP4 每轮只产生一个
实例级 grant，四个 worker 分别设置 READY/COMPLETE 位。sampling 和
其他 RPC 完全绕过 gate。

本地 Linux 测试已覆盖 TP1/TP2/TP4 位图屏障、A/B forward 零重叠、
sampling 与对侧 forward 重叠、超时/迟到完成/缺 rank/重复 rank，
以及 10 万轮授权。锁定的 vLLM 源码测试直接运行真实
`EngineCore.step_with_batch_queue()` 和 WorkerProc busy loop，仅对执行器、
forward 和 sampling 打桩；`MODE=off|elastic` 的原生 async 轨迹一致。

Windows Docker Desktop 固定 CPU 仅作诊断：修复 futex lost-wakeup 后，
TP1 20,000 轮 handoff P50/P99 为 131.93/324.45 微秒，TP4 5,000 轮为
323.59/1107.22 微秒，尚未达到 150 微秒 P99 门槛。该虚拟化结果不能代替
黄区裸机结论，也不能据此宣称性能通过；当前整体仍为 NO-GO。

尚未宣称远端 NPU 通过。黄区必须从干净 runtime 重新开始，不能复用当前
包含旧 deterministic scheduler 的 `libvruntime.so` 和已打旧 patch 的容器。

### 唯一准备入口

```bash
cd /root/l00933108
git pull --ff-only

bash scripts/pair-scheduler/prepare-yellow-zone.sh \
  --physical-npus 4,5,6,7
```

该入口复用 `restart-vcann-xlite-containers.sh` 重建已清理旧调度代码的
runtime 和两个干净容器，在目标 CPython 3.11/aarch64 环境构建并安装
scheduler wheel，只应用新 WorkerProc patch，检查 EngineCore 未改动及
旧符号 tombstone，
最后运行 normal、forward timeout 和 primary death CPU preflight。

成功标志：

```text
PAIR_SCHEDULER_PREPARED
```

### 启动 TP4 双实例

```bash
bash scripts/pair-scheduler/start-yellow-zone.sh
```

该脚本保留既有 Qwen3-4B、TP4、xLite full graph、chunked prefill 和
`--async-scheduling` 参数，并分别配置 A/primary 与 B/standby。A 必须
同时满足 `/v1/models`、inspector RUNNING、A worker registration mask
完整后才启动 B；B 满足相同条件后脚本才报告 `PAIR_READY`。默认启动
超时为 900 秒。

启动后检查：

```bash
curl -s http://127.0.0.1:10040/v1/models
curl -s http://127.0.0.1:10041/v1/models

ctr -n k8s.io tasks exec --exec-id "inspect-$RANDOM" cont1_ljw \
  /bin/bash -lc \
  "vllm-pair-scheduler-inspect \
    --pair-id qwen3-4b-tp4-npu4-7 \
    --shm-dir /dev/shm/vllm-pair-scheduler \
    --json"
```

Inspector 退出码：RUNNING=0、FAILED=2、SHUTDOWN/STALE=3、读取错误=1。
FAILED 后库不会杀死卡在 `execute_model` 的 worker；外部监管必须停止
并整对拉起两个实例。

### 新方案硬件验收闸门

1. 两个 `/v1/models` 就绪，inspector 为 RUNNING，A/B registration
   mask 都是 `0b1111`。
2. 单边请求正常，双边 decode/chunked prefill 无死锁。
3. host grant 区间零重叠。
4. NPU/HCCL timeline 证明 A/B 集合通信不重叠。
5. timeline 证明 sampling 与对侧 forward 可重叠。
6. 证明四个 TP worker 的 `execute_model` 主机调用返回后不存在不可
   重叠的异步通信尾部。
7. 裸机无文件 I/O benchmark 运行 100 万次，handoff P99 目标
   100–150 微秒；未达标时停止性能验收。

## 旧方案历史断点（禁止继续执行）

## 当前断点

- 仓库 `main` 已推送到提交 `d06d6a7`。
- vCANN runtime 已成功编译并安装；双容器已用物理卡 `4,5,6,7` 重建成功。
- 两个容器的 `/dev/shm` 已确认共同挂载宿主机独立目录 `/opt/l00933108`，不再使用 `/opt/isa/shm`。
- vLLM 与 vLLM-Ascend patch 已分别在两个容器成功应用过。
- 首轮双实例启动暴露 `Deterministic scheduler failed: participant exited`，随后 `rtDetSchedEnter(false)` 返回 `500001`。
- 提交 `d06d6a7` 已按当前调测决定修改 vLLM patch：EngineCore 第一次 idle 初始化只本地返回，不发 TP collective、不调用 vCANN；第二次及以后恢复 READY/IDLE 控制泵。
- `d06d6a7` 的 patch 独立应用检查和 Python 编译检查已通过，但尚未完成远端双实例硬件复验。

以下是旧方案当时的下一步，现已废止：

1. 在两个现有容器中把旧 vLLM patch 更新为 `d06d6a7` 版本。
2. 清理本轮已经进入 FAILED 的 `shim4-7`。
3. 重新启动两个 TP4 服务并验证。

## 权威路径与版本

- GitHub 仓库：`https://github.com/MozhiJiawei/vllm-stack-yellow-zone`
- 宿主机项目根目录：`/root/l00933108`
- 本地工作区：`D:\Agent Repo\Insight-Repos\vllm-stack`
- 容器镜像：`vllm:19`
- containerd namespace：`k8s.io`
- vLLM 源码根目录：`/vllm-workspace/vllm`
- vLLM-Ascend 源码根目录：`/vllm-workspace/vllm-ascend`
- vCANN 源码根目录：`/root/l00933108/vcann-rt/ubs-virt-enpu/vcann-rt`
- vCANN runtime 产物：`/root/l00933108/runtime/vcann-deadlock/libvruntime.so`
- 容器 runtime 路径：`/opt/enpu/vcann-rt/hot/libvruntime.so`
- vLLM patch：`/root/l00933108/patches/vllm-vcann-deterministic-scheduling.patch`
- vLLM-Ascend patch：`/root/l00933108/patches/vllm-ascend-vcann-prefill-guard.patch`

锁定上游版本：

- vLLM `v0.19.1`
- vLLM-Ascend `v0.19.1rc1`

vLLM 和 vLLM-Ascend 的改动只通过 patch 承载，不直接提交上游源码目录的修改。

## 已验证拓扑

本轮只使用物理卡 `4,5,6,7`：

| 项目 | cont1_ljw | cont2_ljw |
|---|---:|---:|
| 物理 NPU | 4,5,6,7 | 4,5,6,7 |
| virtual-npu-id | 4,5,6,7 | 12,13,14,15 |
| TP | 4 | 4 |
| aicore-quota | 50 | 50 |
| scheduling-policy | 2（ELASTIC） | 2（ELASTIC） |
| vLLM 端口 | 10040 | 10041 |
| MASTER_PORT | 29504 | 29510 |
| HCCL socket range | 61000-61050 | 62000-62050 |

两个容器使用 host network，并共同把宿主机 `/opt/l00933108` 挂载为容器 `/dev/shm`。已验证的 `findmnt` 结果中，两侧 source 都是：

```text
/dev/mapper/euleros-root[/opt/l00933108]
```

## 容器重建：唯一入口

不要手写 `ctr run`。统一使用：

```bash
cd /root/l00933108
git pull --ff-only

bash scripts/restart-vcann-xlite-containers.sh \
  --restart \
  --physical-npus 4,5,6,7
```

这个入口负责：

- 编译并校验 vCANN runtime；
- 生成 cont1/cont2 独立配置；
- 精确重建 `cont1_ljw`、`cont2_ljw`；
- 安装 xLite 和 GDB；
- 挂载模型、runtime、配置和共享内存；
- 执行 preflight。

仅当当前 runtime 产物已经在同一轮被验证且只需重建容器时，才使用：

```bash
bash scripts/restart-vcann-xlite-containers.sh \
  --restart \
  --skip-build \
  --physical-npus 4,5,6,7
```

本轮隔离共享内存后的成功重建使用了 `--skip-build`，最后输出：

```text
RESTART_COMPLETE runtime=/root/l00933108/runtime/vcann-deadlock/libvruntime.so trace=1 physical_npus=4,5,6,7
```

### 验证共享内存挂载

在宿主机执行：

```bash
for c in cont1_ljw cont2_ljw; do
  echo "=== $c ==="
  ctr -n k8s.io tasks exec --exec-id "shm-$RANDOM" "$c" findmnt /dev/shm
done
```

两侧必须指向 `/opt/l00933108`。如果仍指向 `/opt/isa/shm`，不要继续启动 vLLM。

## 共享内存规则

- 禁止再使用或清理 `/opt/isa/shm`。
- 当前 TP4 的 vCANN POSIX shm backing files 是：

```text
/opt/l00933108/shim4
/opt/l00933108/shim5
/opt/l00933108/shim6
/opt/l00933108/shim7
```

- 只有在两个 vLLM/EngineCore 都已退出时，才允许清理本轮调度状态：

```bash
rm -f /opt/l00933108/shim{4,5,6,7}
```

- 删除这些文件只重置 vCANN 调度共享状态，不会删除模型、容器或 KV cache 文件。
- 容器重建不会自动清除宿主机已有的 `shim*`；出现旧 layout 或状态已进入 FAILED 时必须显式处理。

曾经确认的旧布局错误：

```text
Incompatible shared-memory layout 0x495a4544
```

根因是 `/opt/isa/shm` 留有旧 generation。改用 `/opt/l00933108` 后该问题已解决。

## 进入容器

```bash
ctr -n k8s.io tasks exec \
  --exec-id "cont1-shell-$RANDOM" \
  --tty cont1_ljw /bin/bash
```

```bash
ctr -n k8s.io tasks exec \
  --exec-id "cont2-shell-$RANDOM" \
  --tty cont2_ljw /bin/bash
```

containerd 现场注意事项：

- 使用 namespace `k8s.io`。
- 该版本不使用 `ctr tasks info`，检查 task 使用 `ctr -n k8s.io tasks ls`。
- `ctr tasks exec` 不支持 `--env`；环境变量在容器 shell 内设置。

## 在干净容器中应用 patch

每次重建容器后，cont1 和 cont2 都要独立应用两个 patch。

在每个容器内执行：

```bash
cd /vllm-workspace/vllm
git apply --check /root/l00933108/patches/vllm-vcann-deterministic-scheduling.patch
git apply /root/l00933108/patches/vllm-vcann-deterministic-scheduling.patch

cd /vllm-workspace/vllm-ascend
git apply --check /root/l00933108/patches/vllm-ascend-vcann-prefill-guard.patch
git apply /root/l00933108/patches/vllm-ascend-vcann-prefill-guard.patch
```

两份 patch 在本轮重建后的两个容器中均已成功应用。

### 已应用旧版 vLLM patch 时的增量更新

如果容器已经应用提交 `2b21729` 时的旧 vLLM patch，不需要重建容器，也不要重复处理 vLLM-Ascend。先在宿主机更新仓库：

```bash
cd /root/l00933108
git pull --ff-only
```

然后在 cont1、cont2 各执行：

```bash
cd /vllm-workspace/vllm
git -C /root/l00933108 show \
  2b21729:patches/vllm-vcann-deterministic-scheduling.patch | git apply -R

git apply --check /root/l00933108/patches/vllm-vcann-deterministic-scheduling.patch
git apply /root/l00933108/patches/vllm-vcann-deterministic-scheduling.patch
```

该流程只替换 vLLM patch；vLLM-Ascend patch 保持不动。

## 启动 Qwen3-4B TP4 双实例

不要只启动一个实例后发送推理请求。先启动 cont1，再立即启动 cont2。

### cont1

```bash
cd /workspace
unset ASCEND_RT_VISIBLE_DEVICES
export ENPU_LOG_LEVEL=4

vllm serve /opt/model/Qwen3-4B/ \
  --max_model_len 10240 \
  --tensor-parallel-size 4 \
  --max-num-batched-tokens 1024 \
  --gpu-memory-utilization 0.35 \
  --async-scheduling \
  --block-size 128 \
  --additional-config='{"xlite_graph_config":{"enabled": true, "full_mode": true}}' \
  --host 0.0.0.0 \
  --port 10040 \
  --served-model-name Qwen3-4B \
  > /workspace/llm-4b-det-cont1.log 2>&1 &
```

### cont2

```bash
cd /workspace
unset ASCEND_RT_VISIBLE_DEVICES
export ENPU_LOG_LEVEL=4

vllm serve /opt/model/Qwen3-4B/ \
  --max_model_len 10240 \
  --tensor-parallel-size 4 \
  --max-num-batched-tokens 1024 \
  --gpu-memory-utilization 0.35 \
  --async-scheduling \
  --block-size 128 \
  --additional-config='{"xlite_graph_config":{"enabled": true, "full_mode": true}}' \
  --host 0.0.0.0 \
  --port 10041 \
  --served-model-name Qwen3-4B \
  > /workspace/llm-4b-det-cont2.log 2>&1 &
```

这组参数继承 Issue #9 已成功的 Qwen3-4B TP4、async scheduling 和 xLite full graph 启动基线。本轮 `d06d6a7` 后的双实例完整启动尚待复验，不能提前宣称已通过。

## 启动检查

等待模型加载后，在宿主机检查两个端口：

```bash
curl -s http://127.0.0.1:10040/v1/models
echo
curl -s http://127.0.0.1:10041/v1/models
echo
```

日志必须分别在所属容器中读取；cont2 看不到 cont1 的 `/workspace`：

```bash
ctr -n k8s.io tasks exec \
  --exec-id "log1-$RANDOM" cont1_ljw \
  /bin/bash -lc \
  "grep -nEi 'Incompatible shared-memory|Deterministic scheduler failed|rtDetSchedEnter|Traceback|ERROR|Application startup complete' /workspace/llm-4b-det-cont1.log | tail -n 80"
```

```bash
ctr -n k8s.io tasks exec \
  --exec-id "log2-$RANDOM" cont2_ljw \
  /bin/bash -lc \
  "grep -nEi 'Incompatible shared-memory|Deterministic scheduler failed|rtDetSchedEnter|Traceback|ERROR|Application startup complete' /workspace/llm-4b-det-cont2.log | tail -n 80"
```

## 本轮已确认的故障与结论

### 1. 旧共享内存布局

症状：

```text
Incompatible shared-memory layout 0x495a4544
rtDetSchedEnter failed with error code 500001
```

处理：

- restart 脚本把 `/dev/shm` 宿主机来源改为 `/opt/l00933108`；
- 双容器重建并用 `findmnt` 验证；
- 不再使用 `/opt/isa/shm`。

### 2. 初始 idle 同步过早

症状中的首个底层错误：

```text
Deterministic scheduler failed: participant exited
```

随后四个 TP worker 都出现：

```text
RuntimeError: rtDetSchedEnter failed with error code 500001
```

当前调测决定：

- vLLM EngineCore 第一次 idle 初始化不发 TP collective；
- 第一次调用不进入 vCANN，不读写 A/B 共享状态；
- 后续正常调度恢复现有 READY/IDLE 控制泵；
- vCANN 状态机不因该修复增加字段或分支。

实现提交：`d06d6a7`。尚待远端复验。

### 3. 无关警告

启动日志中以下 runtime symbol warning 在既有环境也会出现，不是本轮 `500001` 的首因：

```text
Failed to find function rtCpuKernelLaunch
Failed to find function rtAicpuKernelLaunch
Failed to find function rtBeginPrefill
Failed to find function rtEndPrefill
```

排障应优先找第一条 `Incompatible shared-memory` 或 `Deterministic scheduler failed`，不要被后续 Python traceback 和 `Hashmap remove failed` 淹没。

## 下一阶段验收顺序

严格按顺序推进：

1. 两个容器更新到 `d06d6a7` vLLM patch。
2. 确认旧 vLLM/EngineCore 已退出，删除 `/opt/l00933108/shim4-7`。
3. 启动 cont1、cont2 的 Qwen3-4B TP4 服务。
4. 两个 `/v1/models` 都成功。
5. 向一个实例发送单 curl，验证 `READY/IDLE` 路径。
6. 向两个实例同时发送请求，验证 `READY/READY` 的 50/50 owner 序列。
7. 功能稳定后再跑吞吐与时延性能。
8. TP4 通过后再参数化覆盖 TP1、TP2、TP8。

任何一步失败，只收集该步第一条根因和必要上下文，不提前堆叠后续命令。

## Issue 协作约定

- 当前调测统一在 Issue #11 回传。
- 聊天中的 `c` 表示：使用 `gh-issue-comment-monitor` 只读取检查点之后的最新回复，根据最新结果在 Issue 中给出下一步动作。
- 每轮只推进一个清晰动作。
- 用户已明确要求启动和调测命令保持简单，优先复用本文已验证命令，不重新发明 watcher、后台服务包装或容器创建流程。
- 不把“patch apply 成功”写成“硬件功能通过”；远端日志才是硬件结论。

## 禁止事项

- 不使用 `/opt/isa/shm`。
- 不手写新的 `ctr run` 容器拓扑。
- 不在 vLLM/vLLM-Ascend 上游目录提交修改；只更新 patch。
- 不在 vLLM/EngineCore 仍运行时删除 `shim4-7`。
- 不从 cont2 读取 cont1 的 `/workspace` 日志，反之亦然。
- 不把旧 Issue #7 的 ABI3 trace 断点当作本轮下一步。
- 不把尚未复验的 `d06d6a7` 宣称为远端成功。
