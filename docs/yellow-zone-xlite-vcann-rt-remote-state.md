# 黄区 Qwen3-32B xLite + vCANN-RT 远端状态

## 当前目标与断点

- 目标：先在真实 NPU 上验证 ABI 3 能把 xLite kernel handle 解析为名称，并从一层 Qwen3 native forward 还原 layer/phase/sync 窗口；通过后再采自然死锁和真实 vLLM。
- 当前断点：旧 ABI 2 worker 已清理；第一次 ABI 3 编译因 `hook.c` 在 `<dlfcn.h>` 前未定义 `_GNU_SOURCE`、导致远端工具链不声明 `RTLD_NEXT` 而停止。失败发生在删除容器之前，所以 `cont1_ljw`、`cont2_ljw` 仍在，但仍是旧的单文件 runtime mount。
- GDB 来源：宿主机二进制 `/root/isa/gdb_arm`（约 96 MiB），已复制为两个容器内的 `/usr/local/bin/gdb`；两侧均为 GDB 10.1，`ldd` 均无缺失库。该 GDB 不支持 Python scripting，但 ptrace、thread 列举和 detach 均正常。
- 当前网络结论：容器直连 `ports.ubuntu.com` 因 DNS 解析失败；显式代理 `http://187.0.6.108:8888` 返回非 HTTP 状态行，两条路径均不可用于 apt。
- 未完成：ABI 3 runtime 远端编译、首次热替换目录挂载迁移、最小 Qwen trace 验收、自然死锁采集、真实 vLLM 接入。

## 权威入口与协作规则

- 同步仓：`https://github.com/MozhiJiawei/vllm-stack-yellow-zone`
- 当前主线：以本文件所在的 `main` 提交为准；旧现场 runtime 来自 ABI 2，不可复用。
- 远端操作与结果回传：`https://github.com/MozhiJiawei/vllm-stack-yellow-zone/issues/7`
- 黄区唯一项目根目录：`/root/l00933108`
- 本地工作区：`D:\Agent Repo\Insight-Repos\vllm-stack`
- 聊天中的 `c` 表示：读取 Issue #7 最新回复，根据最新结果只给下一件事的完整命令。
- 每轮 Issue 只推进一个动作；不提前堆叠后续步骤。
- 只操作明确命名的实验容器、文件和进程；不扫描无关环境，不采集凭据。
- 本文件只保留当前事实、约束、结论和下一步，不记录排障流水。

## 当前远端基础设施

- 主机：`node149`，aarch64，8 张 Ascend 910B4。
- 容器运行时：containerd `2.3.3`，命令行使用 `ctr`，namespace 为 `k8s.io`；不再使用 Docker 流程。
- 该版本没有 `ctr tasks info`；任务存在性必须通过 `ctr tasks ls` 的首列精确匹配。
- 该版本的 `ctr tasks exec` 不支持 `--env`；临时环境变量必须在 exec 启动的 `/bin/bash -lc` 内设置，或通过 `/usr/bin/env` 作为容器内命令传入。
- 本地镜像：`vllm:19`；现场无外网，不拉取 `quay.io/ascend/vllm-ascend:v0.19.1rc1`。
- 宿主机 coordinator：Python `3.9.9`，测试端口已确认可用。
- 容器 Python：`3.11.14`；torch `2.9.0+cpu`、torch-npu `2.9.0`；每个容器可见 8 个 NPU device index。
- xLite wheel 源：`/root/isa/conf/xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl`。
- 容器创建时的 `/workspace/xlite.whl` 简写挂载不作为安装入口。现场已有固定安装流程，会把源 wheel 以完整文件名复制进两个容器的 `/workspace/` 后安装；后续 AI 应直接复用该流程。

## 固定双容器拓扑

- 容器 A：`cont1_ljw`，vNPU `0-7`，HCCL socket range `61000-61050`，xLite port `23000`。
- 容器 B：`cont2_ljw`，vNPU `8-15`，HCCL socket range `62000-62050`，xLite port `24000`。
- 两个容器都映射物理设备 `/dev/davinci0-7` 以及 manager/devmm/hdc，使用 host network 和共享 `/opt/isa/shm`。
- 两个容器都 bind mount `/root/l00933108`，因此宿主机同步后的脚本和产物直接可见。
- 独立配置：`/root/l00933108/cont1_npu_info.config` 与 `/root/l00933108/cont2_npu_info.config`。
- 最后确认的配置为每个 vNPU `aicore-quota=50`、`memory-quota=15000`；配置源原本使用 `scheduling-policy=2`（ELASTIC）。后续若文件被现场修改，必须以这两个实际文件为准，不能沿用旧回复推断。
- 当前两个容器设置了 `ENPU_DEADLOCK_TRACE=1`、`CAP_SYS_PTRACE`，但仍使用旧的单文件 runtime mount；下一次成功操作必须用 `--restart` 完成一次目录挂载迁移。

### 容器启动基线

- 宿主机统一入口为 `bash /root/l00933108/scripts/restart-vcann-xlite-containers.sh`。默认优先在运行中的 `cont1_ljw` 编译并原子替换 runtime，不重启容器；若指定的编译 task 不在运行，脚本会基于本地 `vllm:19` 自动创建仅挂载代码和驱动的临时编译容器，完成或失败后精确删除。已有进程继续使用旧映射，之后启动的新进程加载新 runtime。
- 只有显式增加 `--restart` 才精确删除并重建两个容器、安装 xLite/GDB 和执行完整 preflight。首次从旧单文件 mount 迁移到热替换目录 mount 必须执行一次 `--restart`。
- 脚本始终先完成编译和符号校验，再考虑删除容器；编译失败不会破坏当前环境。若已单独验证现有 runtime 产物，可显式使用 `--skip-build`；不能在没有验证产物时绕过编译。
- 使用 `ctr -n k8s.io run --detach --net-host`，镜像为本地 `vllm:19`，容器名固定为 `cont1_ljw`、`cont2_ljw`，入口为 `/bin/bash`。
- 不使用 `--privileged`：现场 OCI runtime 无法识别其展开出的 `CAP_PERFMON`。诊断只显式增加 `--cap-add CAP_SYS_PTRACE`。
- 两侧共同环境变量：`ASCEND_RUNTIME_OPTIONS=NODRV`、`ENPU_DEADLOCK_TRACE=1`、`HCCL_OP_EXPANSION_MODE=AIV`、`TASK_QUEUE_ENABLE=1`、`XLITE_DISABLE_XCCL=true`。
- A 使用 `MASTER_PORT=29504`、`HCCL_NPU_SOCKET_PORT_RANGE=61000-61050`；B 使用 `MASTER_PORT=29510`、`HCCL_NPU_SOCKET_PORT_RANGE=62000-62050`。
- 两侧必须挂载：`/dev/davinci0-7`、davinci manager/devmm/hdc、driver、Qwen3-4B/32B 模型目录、诊断 runtime 目录、`enpu-monitor`、各自 config、生成的 `ld.so.preload`、`npu-smi`、`systemd-detect-virt`、共享 shm 和 `/root/l00933108`。
- runtime 源目录固定为 `/root/l00933108/runtime/vcann-deadlock`，容器目标为 `/opt/enpu/vcann-rt/hot`。脚本在目录内原子替换 `libvruntime.so`，目录 bind mount 能让后续新进程看到新 inode；生成的 preload 文件保留原 preload 中其他条目，只把唯一 `libvruntime.so` 改到 hot 路径。
- A config 源为 `/root/l00933108/cont1_npu_info.config`；B config 源为 `/root/l00933108/cont2_npu_info.config`。不得让两个容器共享同一个可写 config 文件。
- `--physical-npus` 接收 `0–7` 范围内的物理卡 ID 列表，默认 `0,1,2,3,4,5,6,7`；例如 `--physical-npus 4,5,6,7` 只挂载 `/dev/davinci4-7`。`--restart` 时脚本原子生成两份 config：section 使用连续的 `DEVICE-0..N-1`，A 保持 `virtual-npu-id=physical-npu-id`，B 保持 `virtual-npu-id=physical-npu-id+8`，两侧 `shm-id` 按物理 ID 保持一致。
- preload 模板源固定归档为 `/root/l00933108/ld.so.preload`，脚本据此生成 runtime 目录下的挂载文件；可通过 `PRELOAD_SOURCE` 覆盖，不再依赖 `/root/isa/bins/ld.so.preload`。
- 完整 `ctr run` 参数已经固化在 `scripts/restart-vcann-xlite-containers.sh`；Issue #6 评论只作为历史成功证据，不再复制命令手工重建。

### xLite 安装基线

- 使用现场已有流程，不依赖 wheel bind mount，也不直接对简写 `/workspace/xlite.whl` 执行 pip。
- 从 `ctr -n k8s.io t ls` 读取两个 task PID，通过 `/proc/<pid>/root/workspace/` 把宿主机 wheel 以完整文件名复制进容器，再分别执行 pip。
- 固定 wheel：`xlite-0.1.0rc12-cp311-cp311-manylinux2014_aarch64.whl`；安装后版本应为 `0.1.0rc12`。
- 同一阶段把 `/root/isa/gdb_arm` 安装为两侧 `/usr/local/bin/gdb`，并验证版本和动态库依赖；容器重建后不能假设上次手工复制的 GDB 仍存在。

## 代码与诊断实现

- vLLM：`b1388b1fbf5aaef47937fabe98931211684666a6`（v0.19.1）。
- vLLM-Ascend：`da421afad7192dac64e39ae1d32305d57344f3cf`（v0.19.1rc1）。
- vCANN-RT 基线：`fa7917ed233d3100756606bfcfe2d121ff361ab4`，仓库在其上增加当前诊断改动。
- 已删除旧的 vLLM/site-packages 诊断链；定位能力全部位于 vCANN-RT 和独立采集工具中。
- 诊断构建开关：`ENABLE_DEADLOCK_DIAGNOSTICS=1`。普通构建不包含 probe 调用、trace buffer 或相关符号。
- 诊断构建的运行时开关：`ENPU_DEADLOCK_TRACE=1`。未设置时不记录。
- trace 在 Runtime hook 入口、`core_limiter()` 之前记录“调用尝试”；它不证明 Runtime 已接受、设备已入队或算子已完成。
- scheduler probe 标记进入/退出 `rtStreamSynchronize`，记录 vNPU、进入时的 owner、schedule turn 和 stream。
- 环形缓冲每进程 4096 条；不做文件 I/O或动态分配，使用无全局锁的逐 slot 同步。
- 诊断版仍为 `-O2`，保留 GDB 符号；GDB helper 校验 magic、ABI 和 capacity。

### 当前诊断产物

- ABI 3 权威产物为 `/root/l00933108/runtime/vcann-deadlock/libvruntime.so`；兼容路径 `/root/l00933108/libvruntime-deadlock-diag.so` 是指向它的 symlink。当前远端尚未成功生成 ABI 3 产物。
- 当前源码的关键符号为 `aclrtBinaryGetFunction`、`g_vcann_trace`、`g_vcann_sync_probe`、`g_vcann_host_sync_probe`、`g_vcann_kernel_registry`、`vcann_trace_record_enabled`。
- 容器内挂载目标：`/opt/enpu/vcann-rt/hot/libvruntime.so`。
- 采集入口：`scripts/deadlock-diagnostics/collect_two_ctr_containers.sh`。
- 容器内采集器：`collect_vcann_deadlock.py`；GDB decoder：`vcann_trace_gdb.py`。
- 现场 GDB 无 Python 时，GDB 导出 trace、scheduler probe、host-sync probe、kernel registry 四个固定 ABI 对象，由容器普通 Python 解码；支持 GDB Python 的环境沿用 helper 路径。

### vCANN 诊断编译基线

- 在 `cont1_ljw` 的 CANN 构建环境中编译，源码为 `/root/l00933108/vcann-rt/ubs-virt-enpu/vcann-rt`。
- 必须先 source `/usr/local/Ascend/ascend-toolkit/set_env.sh`（若存在）并确认 `ASCEND_HOME_PATH` 非空；`ENPU_ASCEND_DRIVER_PATH` 未设置时构建脚本默认使用 `/usr/local/Ascend`。
- 诊断编译命令为 `ENABLE_DEADLOCK_DIAGNOSTICS=1 bash make_build.sh`。
- `build/CMakeCache.txt` 绑定绝对源码路径。若 cache 来自旧路径 `/workspace/vcann-rt`，必须先把整个 `build` 目录改名归档，再从空 build 目录构建；不能复用或只改 cache 文本。
- 构建完成后确认 `build/libvruntime.so` 非空，原子替换 `/root/l00933108/runtime/vcann-deadlock/libvruntime.so`，并用 `readelf -sW` 逐一验证上述诊断符号。默认增量构建；只有明确需要时才给一键脚本增加 `--clean-build`。

## 当前测试基线

- 测试脚本：`scripts/repro-tp8-xlite-xccl-deadlock-two-container.py`。
- coordinator 在宿主机运行；A/B participant 分别在两个容器运行，控制面使用 host network 的 `127.0.0.1`。
- workload 是 Qwen3-32B-like 原生 `Model.forward` 调用序列，不加载真实 vLLM 服务：TP=8、64 层、batch=16、hidden size=5120、intermediate size=25600、BF16、cached tokens=9216。
- 已验证 aligned：16 个 worker 全部 `DONE`，`RESULT=NORMAL_CONTROL_PASSED`。
- 已验证 crossed（未启用新诊断链时）：第一波和第二波相隔 5 秒，16 个 worker 最终全部 `DONE`，`RESULT=NO_DEADLOCK`。这是有效负结果，不是双容器同步失败。
- 当前功能验收入口为 `scripts/deadlock-diagnostics/verify_xlite_qwen_trace.py`：单容器 TP8、Qwen3-32B 形状、1 层；全 rank warm-up 后只让 rank 0 提交第二次 forward，形成可控采集窗口。它只验证工具，不代表自然死锁。

## 诊断结果能回答的问题

- 哪些 worker 位于应用 device/stream sync 或 vCANN scheduler stream sync，及对应 stream、vNPU、owner 和 schedule turn。
- `aclrtBinaryGetFunction` 返回的 function handle 到 kernel 名称映射；同时保留 `rtFunctionRegister` 注册信息，覆盖旧式 xLite/runtime 路径。
- Qwen3 dense forward 的 layer、attention/MLP 阶段和两次 TP AllReduce；warm-up 的成功 device sync 是 layer 0 锚点。
- 上一次成功同步到当前阻塞同步之间的 unresolved kernel window，并单列其中的 collective 候选。
- 其他线程是在 `core_limiter`、Runtime、futex 还是其他 host 路径等待。
- A/B、16 个 rank 的调用序列是否出现稳定分组或分叉。

launch hook 只能证明 host 调用了 Runtime。scheduler sync 可以界定 unresolved window，但不能单独证明窗口中的哪个 kernel 此刻仍在执行；device sync 只能给出最后提交项。输出中的 `completion_evidence` 必须与 native backtrace、vCANN/XCCL 日志和设备侧信息一起解释，不能把 `blocked_kernel` 字段当成设备完成回执。

## 采集与现场保持约束

- 自动发现目标要求进程同时加载 `libvruntime.so` 并打开 `/dev/davinciN`；每个容器预期 8 个 worker。现场已验证部分 worker 的 device FD 不适合用于发现，返回 0 个目标时应从 participant 父进程精确取得 8 个 worker PID，再用 `--pid` 采集；不能全局放宽为 `--include-no-device`，否则会混入 PID 1、resource tracker 和旧孤儿进程。
- GDB 基线已经验证：宿主机 `/root/isa/gdb_arm` 可复制到两个容器的 `/usr/local/bin/gdb`；GDB 10.1、`ldd` 无缺失库，具备 `CAP_SYS_PTRACE` 的容器可以 attach、列举线程和 detach。容器必须在创建时授予 ptrace 能力，运行后不能补加。
- 双容器采集默认并发 `SIGSTOP` 16 个 worker，GDB detach 后继续保持冻结，并在宿主机生成合并 JSON/CSV 和 tar.gz。
- 首次功能验收不使用 `--resume-after`；确认归档落盘后再按 manifest 发送 `SIGCONT`。
- `SIGCONT` 只恢复调度，不会解除原有设备死锁；归档完成后可按测试清理流程结束进程。

## 已验证的 GDB 采集经验

- 现场 GDB 不支持 Python scripting 不代表采集失败。可靠回退路径是让 GDB 用多个独立的 `-ex 'dump binary memory ...'` 导出固定 ABI 对象，再由容器内普通 Python 解码；trace、scheduler probe、host-sync probe 和 kernel registry 已按此路径成功采齐。
- runtime 与 collector 的 ABI 必须完全一致。ABI 2 进程不能交给 ABI 3 collector 解码；升级后要重新编译、替换 runtime 并重启 worker，不能只更新采集脚本。
- 冻结和恢复以采集 manifest 为准。发送信号前同时校验 PID 和 `/proc/<pid>/stat` 的 starttime，防止 PID 复用；GDB detach 会让被调试进程短暂恢复，collector 必须再次 `SIGSTOP`，所以采集结束后 worker 仍应保持冻结。
- 上次 16/16 worker 的原始内存、native backtrace、汇总 JSON 和归档均成功生成，证明采集链路可用。该快照是人为只释放第一波 worker 的功能测试，不是自然死锁证据。
- native backtrace 中，应用主线程停在 `c10_npu::npuSynchronizeDevice -> aclrtSynchronizeDeviceWithTimeoutImpl`，scheduler 后台线程出现在 `check_and_borrow_timeslice`。前者描述上层正在等待设备同步，后者只是同一时刻的调度线程位置，不能互相替代。
- `sync_active=false` 表示采样瞬间没有捕获到 `synchronize_and_clear_streams -> rtStreamSynchronize` 的活动窗口；此时 `sync_owner`、`sync_turn` 只是最近一次完成同步留下的值，不能解释成当前 owner。
- trace 末端的 `RTS_KERNEL_HOST_ARGS` 与 `EVENT_WAIT` 成功区分了已进入 Runtime 的第一波 worker 和仍卡在 Python `multiprocessing.Event` 的第二波 worker。这证明工具能够还原人为调度状态；要定位具体算子，仍需 ABI 3 的 kernel registry、launch 名称和 Qwen phase 一起通过真实 NPU 验收。
- 容器无外网时直接在内网解码和汇总，不依赖传出原始归档。containerd 2.3 用 `ctr tasks ls` 判断 task 是否存在；不要使用不存在的 `ctr tasks info`。

## 后续固定顺序

1. 同步当前 main，重新编译 ABI 3 runtime，并确认四个全局诊断对象和记录函数符号。
2. 在 `cont1_ljw` 运行最小 Qwen trace verifier；出现 `CAPTURE_READY` 后采集 8 个 worker，`--qwen-layers 1`。
3. 内网检查 kernel registry 是否含 matmul、attention、allreduce，launch 记录是否带 `kernel_names`，Qwen layer/phase 和 sync probe 是否符合人为缺 rank 场景。
4. 功能验收通过后，再用真实可复现死锁抓 A/B 两侧现场；不再用人为 hold 结果推断自然死锁根因。

任何一步失败时，只处理该失败，不跳到下一步，也不为了“补信息”扩大远端采集范围。
