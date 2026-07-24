# vLLM 双实例 elastic 调度部署

本文从两个 Linux 容器已经创建开始。第一版只支持同机、两个实例、一个
pair、PP=1；主备都是正常提供推理服务的实例，“主”只表示它负责共享内存
调度决策。

## 1. 容器必须共享同一目录

### 1.1 两个必须增加的 `ctr run` 参数

相对于原有 vLLM 容器，两个容器都必须增加下面两个 bind mount：

```bash
--mount type=bind,src=/opt/l00933108,dst=/dev/shm,options=rbind:rw
--mount type=bind,src=/root/l00933108,dst=/root/l00933108,options=rbind:rw
```

第一个参数让两个容器使用同一个宿主机 `/dev/shm` 后端。它既承载 vCANN
配置中的 `shim4`～`shim7`，也承载调度器固定使用的
`/dev/shm/vllm-pair-scheduler`。不能给两个容器分别使用容器私有的
`/dev/shm`。

第二个参数让两个容器都能以 `/root/l00933108` 访问同一份适配源码、vLLM
补丁和构建后的 vCANN runtime。若现场源码根目录不同，同时替换 mount
两侧路径和后文安装命令中的源码路径即可。

宿主机先准备共享目录：

```bash
install -d -m 1777 /opt/l00933108
install -d -m 1770 /opt/l00933108/vllm-pair-scheduler
```

### 1.2 TP4 + vCANN 配套启动命令

下面是与后文 Qwen3-4B TP4 启动参数配套的完整示例。两个容器共用物理 NPU
4、5、6、7；每个容器获得 50% AICore 和 15 GB 内存配额。两个
`npu_info.config` 使用相同的 physical NPU、`shm-id` 和调度策略，但
virtual NPU ID 不同。

先准备主实例的 vCANN 配置：

```bash
tee /root/l00933108/cont1_npu_info.config >/dev/null <<'EOF'
[DEVICE-0]
physical-npu-id=4
virtual-npu-id=4
aicore-quota=50
memory-quota=15000
shm-id=shim4
scheduling-policy=2

[DEVICE-1]
physical-npu-id=5
virtual-npu-id=5
aicore-quota=50
memory-quota=15000
shm-id=shim5
scheduling-policy=2

[DEVICE-2]
physical-npu-id=6
virtual-npu-id=6
aicore-quota=50
memory-quota=15000
shm-id=shim6
scheduling-policy=2

[DEVICE-3]
physical-npu-id=7
virtual-npu-id=7
aicore-quota=50
memory-quota=15000
shm-id=shim7
scheduling-policy=2
EOF
chmod 0644 /root/l00933108/cont1_npu_info.config
```

再准备备实例的 vCANN 配置：

```bash
tee /root/l00933108/cont2_npu_info.config >/dev/null <<'EOF'
[DEVICE-0]
physical-npu-id=4
virtual-npu-id=12
aicore-quota=50
memory-quota=15000
shm-id=shim4
scheduling-policy=2

[DEVICE-1]
physical-npu-id=5
virtual-npu-id=13
aicore-quota=50
memory-quota=15000
shm-id=shim5
scheduling-policy=2

[DEVICE-2]
physical-npu-id=6
virtual-npu-id=14
aicore-quota=50
memory-quota=15000
shm-id=shim6
scheduling-policy=2

[DEVICE-3]
physical-npu-id=7
virtual-npu-id=15
aicore-quota=50
memory-quota=15000
shm-id=shim7
scheduling-policy=2
EOF
chmod 0644 /root/l00933108/cont2_npu_info.config
```

下面命令假设以下文件已经准备完成：

```text
/root/l00933108/
├── scheduler/
├── patches/vllm-pair-elastic-scheduling.patch
├── runtime/vcann-deadlock/
│   ├── libvruntime.so
│   └── ld.so.preload
├── cont1_npu_info.config
└── cont2_npu_info.config

/root/isa/conf/
└── xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl
```

创建主实例容器：

```bash
ctr -n k8s.io run \
  --env ASCEND_RUNTIME_OPTIONS=NODRV \
  --env ENPU_DEADLOCK_TRACE=1 \
  --env MASTER_ADDR=localhost \
  --env MASTER_PORT=29504 \
  --env HCCL_NPU_SOCKET_PORT_RANGE=61000-61050 \
  --env HCCL_OP_EXPANSION_MODE=AIV \
  --env TASK_QUEUE_ENABLE=1 \
  --env XLITE_DISABLE_XCCL=true \
  --cap-add CAP_SYS_PTRACE \
  --detach \
  --device /dev/davinci4 \
  --device /dev/davinci5 \
  --device /dev/davinci6 \
  --device /dev/davinci7 \
  --device /dev/davinci_manager \
  --device /dev/devmm_svm \
  --device /dev/hisi_hdc \
  --mount type=bind,src=/usr/local/Ascend/driver/,dst=/usr/local/Ascend/driver/,options=rbind:ro \
  --mount type=bind,src=/cache/isa/Qwen3-4B,dst=/opt/model/Qwen3-4B/,options=rbind:ro \
  --mount type=bind,src=/root/l00933108/runtime/vcann-deadlock,dst=/opt/enpu/vcann-rt/hot,options=rbind:ro \
  --mount type=bind,src=/root/isa/bins/enpu-monitor,dst=/opt/enpu/vcann-rt/tools/enpu-monitor,options=rbind:rw \
  --mount type=bind,src=/root/l00933108/cont1_npu_info.config,dst=/etc/enpu/vcann-rt/npu_info.config,options=rbind:rw \
  --mount type=bind,src=/root/l00933108/runtime/vcann-deadlock/ld.so.preload,dst=/etc/ld.so.preload,options=bind:ro \
  --mount type=bind,src=/usr/local/sbin/npu-smi,dst=/usr/local/sbin/npu-smi,options=rbind:ro \
  --mount type=bind,src=/usr/bin/systemd-detect-virt,dst=/usr/bin/systemd-detect-virt,options=rbind:rw \
  --mount type=bind,src=/opt/l00933108,dst=/dev/shm,options=rbind:rw \
  --mount type=bind,src=/root/l00933108,dst=/root/l00933108,options=rbind:rw \
  --net-host \
  vllm:19 cont1_ljw /bin/bash
```

创建备实例容器：

```bash
ctr -n k8s.io run \
  --env ASCEND_RUNTIME_OPTIONS=NODRV \
  --env ENPU_DEADLOCK_TRACE=1 \
  --env MASTER_ADDR=localhost \
  --env MASTER_PORT=29510 \
  --env HCCL_NPU_SOCKET_PORT_RANGE=62000-62050 \
  --env HCCL_OP_EXPANSION_MODE=AIV \
  --env TASK_QUEUE_ENABLE=1 \
  --env XLITE_DISABLE_XCCL=true \
  --cap-add CAP_SYS_PTRACE \
  --detach \
  --device /dev/davinci4 \
  --device /dev/davinci5 \
  --device /dev/davinci6 \
  --device /dev/davinci7 \
  --device /dev/davinci_manager \
  --device /dev/devmm_svm \
  --device /dev/hisi_hdc \
  --mount type=bind,src=/usr/local/Ascend/driver/,dst=/usr/local/Ascend/driver/,options=rbind:ro \
  --mount type=bind,src=/cache/isa/Qwen3-4B,dst=/opt/model/Qwen3-4B/,options=rbind:ro \
  --mount type=bind,src=/root/l00933108/runtime/vcann-deadlock,dst=/opt/enpu/vcann-rt/hot,options=rbind:ro \
  --mount type=bind,src=/root/isa/bins/enpu-monitor,dst=/opt/enpu/vcann-rt/tools/enpu-monitor,options=rbind:rw \
  --mount type=bind,src=/root/l00933108/cont2_npu_info.config,dst=/etc/enpu/vcann-rt/npu_info.config,options=rbind:rw \
  --mount type=bind,src=/root/l00933108/runtime/vcann-deadlock/ld.so.preload,dst=/etc/ld.so.preload,options=bind:ro \
  --mount type=bind,src=/usr/local/sbin/npu-smi,dst=/usr/local/sbin/npu-smi,options=rbind:ro \
  --mount type=bind,src=/usr/bin/systemd-detect-virt,dst=/usr/bin/systemd-detect-virt,options=rbind:rw \
  --mount type=bind,src=/opt/l00933108,dst=/dev/shm,options=rbind:rw \
  --mount type=bind,src=/root/l00933108,dst=/root/l00933108,options=rbind:rw \
  --net-host \
  vllm:19 cont2_ljw /bin/bash
```

两条命令的 NPU device、模型目录、vCANN runtime 和共享 `/dev/shm` 完全
一致；只区分 vCANN 配置文件、virtual NPU ID、`MASTER_PORT` 和 HCCL
socket 端口范围。

容器创建后，先取得两个 task 的宿主机 PID，将同一个 xLite wheel 复制到
两个容器的 `/workspace`。`ctr` 没有 `docker cp` 对应命令，因此这里通过
`/proc/<task-pid>/root` 访问容器根文件系统：

```bash
CONT1_PID=$(ctr -n k8s.io tasks list |
  awk '$1 == "cont1_ljw" {print $2}')
CONT2_PID=$(ctr -n k8s.io tasks list |
  awk '$1 == "cont2_ljw" {print $2}')

test "$CONT1_PID" -gt 0
test "$CONT2_PID" -gt 0

install -m 0644 \
  /root/isa/conf/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl \
  "/proc/$CONT1_PID/root/workspace/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl"

install -m 0644 \
  /root/isa/conf/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl \
  "/proc/$CONT2_PID/root/workspace/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl"
```

然后在两个容器中分别安装，并校验版本：

```bash
ctr -n k8s.io tasks exec --exec-id install-xlite-a cont1_ljw \
  /bin/bash -lc '
    python3 -m pip install \
      /workspace/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl
    python3 -c "import importlib.metadata as m; assert m.version(\"xlite\") == \"0.1.0rc12\""
  '

ctr -n k8s.io tasks exec --exec-id install-xlite-b cont2_ljw \
  /bin/bash -lc '
    python3 -m pip install \
      /workspace/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl
    python3 -c "import importlib.metadata as m; assert m.version(\"xlite\") == \"0.1.0rc12\""
  '
```

## 2. 一键安装

先确认两个容器里都没有正在运行的 vLLM：

```bash
ctr -n k8s.io tasks exec --exec-id check-a cont1_ljw \
  /bin/bash -lc "pgrep -af 'EngineCore|vllm serve|VLLM::Worker' || true"

ctr -n k8s.io tasks exec --exec-id check-b cont2_ljw \
  /bin/bash -lc "pgrep -af 'EngineCore|vllm serve|VLLM::Worker' || true"
```

先安装主实例：

```bash
ctr -n k8s.io tasks exec --exec-id install-primary cont1_ljw \
  /bin/bash -lc \
  "bash /root/l00933108/scheduler/install-pair-scheduler.sh /root/l00933108 primary"
```

再安装备实例：

```bash
ctr -n k8s.io tasks exec --exec-id install-standby cont2_ljw \
  /bin/bash -lc \
  "bash /root/l00933108/scheduler/install-pair-scheduler.sh /root/l00933108 standby"
```

脚本一次完成：

- 从指定源码根目录编译并安装 scheduler wheel；
- 给 `/vllm-workspace/vllm` 应用 vLLM v0.19.1 补丁；
- 编译检查补丁后的 Python 文件；
- 建立 `/dev/shm/vllm-pair-scheduler`；
- 写入当前容器的主/备角色。

备实例安装时必须看到主实例写入共享目录的标记。看不到就直接失败，通常
表示两个容器没有绑定同一个宿主机目录，或者安装顺序错误。

如果 vLLM 源码不在默认目录，用第三个参数指定：

```bash
bash /root/l00933108/scheduler/install-pair-scheduler.sh \
  /root/l00933108 primary /custom/path/to/vllm
```

## 3. 启动

当前版本支持 TP（Tensor Parallel）和 EP（Expert Parallel），暂不支持
DP（Data Parallel）；DP 支持仍在开发中。

两个共卡部署模型必须使用相同的通信域，包括相同的可见 NPU 集合及一致的
通信组范围。除此之外，调度器不对 `vllm serve` 增加任何启动参数要求：
模型原来如何启动，安装调度器后仍按原参数启动即可，也不再设置任何
`VLLM_PAIR_SCHED_*` 环境变量。

vCANN 已经按 `npu_info.config` 完成显存切分，vLLM 看到的是当前实例切分后
的显存配额，而不是整卡显存。因此两个实例都应在各自配额内使用
`--gpu-memory-utilization 0.85`；不能再次按整卡比例折算成 `0.35`。这仍
属于 vCANN 部署参数，不是 pair scheduler 新增的参数。

下面命令只是黄区现有 TP4、async scheduling 和 xLite full graph 配置的
启动示例，这些参数不是调度器的强制配置。

主实例示例：

```bash
ctr -n k8s.io tasks exec --exec-id start-a cont1_ljw \
  /bin/bash -lc '
    cd /workspace
    export MASTER_PORT=29504
    export HCCL_SOCKET_PORT_RANGE=61000-61050
    exec vllm serve /opt/model/Qwen3-4B \
      --tensor-parallel-size 4 \
      --async-scheduling \
      --max-model-len 10240 \
      --max-num-batched-tokens 1024 \
      --gpu-memory-utilization 0.85 \
      --block-size 128 \
      --additional-config='"'"'{"xlite_graph_config":{"enabled":true,"full_mode":true}}'"'"' \
      --served-model-name Qwen3-4B \
      --host 0.0.0.0 --port 10040
  '
```

确认主实例已经完成 worker 注册：

```bash
ctr -n k8s.io tasks exec --exec-id ready-a cont1_ljw \
  /bin/bash -lc '
    curl -fsS http://127.0.0.1:10040/v1/models >/dev/null
    vllm-pair-scheduler-inspect --json
  '
```

然后用相同模型参数启动备实例，只需避开服务端口、`MASTER_PORT` 和
`HCCL_SOCKET_PORT_RANGE`：

```bash
ctr -n k8s.io tasks exec --exec-id start-b cont2_ljw \
  /bin/bash -lc '
    cd /workspace
    export MASTER_PORT=29510
    export HCCL_SOCKET_PORT_RANGE=62000-62050
    exec vllm serve /opt/model/Qwen3-4B \
      --tensor-parallel-size 4 \
      --async-scheduling \
      --max-model-len 10240 \
      --max-num-batched-tokens 1024 \
      --gpu-memory-utilization 0.85 \
      --block-size 128 \
      --additional-config='"'"'{"xlite_graph_config":{"enabled":true,"full_mode":true}}'"'"' \
      --served-model-name Qwen3-4B \
      --host 0.0.0.0 --port 10041
  '
```

业务请求应在两个实例都满足 `/v1/models` 可用、inspector 为 RUNNING 且
A/B worker 注册完整后再发送。

## 4. 检查和关闭

任一容器中执行：

```bash
vllm-pair-scheduler-inspect --json
```

退出码含义：

- `0`：RUNNING；
- `2`：FAILED；
- `3`：SHUTDOWN 或主心跳失活；
- `1`：读取失败。

主实例退出、持权 worker 超时或共享状态损坏后，不自动切换主备。停止两个
vLLM，再整对启动。

要彻底关闭调度集成，在两个容器中删除角色文件并重启：

```bash
rm -f /etc/vllm-pair-scheduler/role
```

角色文件不存在时，补丁只在 worker 初始化阶段检查一次，然后保持原生
`execute_model` 不变；推理热路径没有调度器条件判断。

## 附录：新增参数

安装脚本只有三个位置参数：

1. `source-root`：本项目源码根目录，必须包含 `scheduler/` 和补丁文件。
2. `primary|standby`：当前容器的角色。主固定映射为实例 A，备固定映射为
   实例 B。
3. `vllm-source`：可选；vLLM 源码目录，默认 `/vllm-workspace/vllm`。

其余协议值不再对部署暴露：模式固定为 `elastic`，pair ID 固定为
`default`，共享目录固定为 `/dev/shm/vllm-pair-scheduler`，初始化超时、
forward 超时、心跳周期和对端超时分别固定为 30 秒、30 秒、100 毫秒和
1 秒。
