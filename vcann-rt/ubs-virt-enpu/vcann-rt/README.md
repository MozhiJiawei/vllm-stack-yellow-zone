# vCANN-RT

## 介绍

`vCANN-RT(virtual CANN Runtime)`是ubs-virt-enpu提供NPU算力软切分的方案。本方案需要依赖Linux系统Preload Hook的能力，通过预加载软切分动态库，拦截部分runtime API的调用，根据算力和显存资源配额信息，进行算力控制或显存控制。

## 环境准备

### 软件版本

#### Atlas A2 推理系列产品

**表 1 软件版本**

| 软件                | 版本                                                                        |
|:---------------------|:-----------------------------------------------------------------------------|
| CANN                | 8.5.0 、9.1.0                                                                  |
| HDK                 | 25.5.0及以上版本                                                            |
| （可选）Kubernetes  | 1.17.x~1.34.x，推荐使用1.19.x及以上版本。<br>（直接使用Docker部署则不需要）|
| （可选）MindCluster | 26.0.0（直接使用Docker部署则不需要）|

#### Atlas A3 推理系列产品

**表 2 软件版本**

| 软件                | 版本                                                                        |
|:---------------------|:-----------------------------------------------------------------------------|
| CANN                | 8.5.0 、9.1.0                                                                  |
| HDK                 | 26.0.0及以上版本                                                            |
| （可选）Kubernetes  | 1.17.x~1.34.x，推荐使用1.19.x及以上版本。<br>（直接使用Docker部署则不需要）|
| （可选）MindCluster | 26.0.0（直接使用Docker部署则不需要）|

#### Atlas 350 加速卡产品

**表 3 软件版本**

| 软件                | 版本                                                                        |
|:---------------------|:-----------------------------------------------------------------------------|
| CANN                | 9.1.0                                                                  |
| HDK                 | 25.7.0及以上版本                                                            |
| （可选）Kubernetes  | 1.17.x~1.34.x，推荐使用1.19.x及以上版本。<br>（直接使用Docker部署则不需要）|
| （可选）MindCluster | 26.1.0（直接使用Docker部署则不需要）|

### 主机侧环境配置

主机侧通过`npu-smi`工具开启容器共享模式，可支持多个容器挂载同一设备。若设备未开启容器共享模式，则只能挂载到单个容器。若配合MindCluster使用，要求整节点开启容器共享模式。

```shell
# Atlas A2 / A3 推理系列产品：设置容器共享模式
npu-smi set -t device-share -i ${id} -c ${chip_id} -d ${value}
# Atlas 350 加速卡产品：设置容器共享模式
npu-smi set -t device-share -i ${id} -d ${value}

# 查询设备容器共享模式
npu-smi info -t device-share -i ${id}
```

**表 4 参数说明**

|参数|参数选项|说明|
|:---|:---|:---|
|id|设备id|通过`npu-smi info -l`命令查询获取的NPU ID即为设备id。|
|chip_id|芯片id|通过`npu-smi info -m`命令查询获取的Chip ID即为芯片id。|
|value|<ul>默认值：禁用<li>禁用(0)</li><li>使能(1)</li></ul>|容器共享模式使能状态。|
    
  示例：
  开启设备0所有芯片的容器共享模式，查询设备0容器共享模式。

  ```shell
  npu-smi set -t device-share -i 0 -d 1
  npu-smi info -t device-share -i 0
  ```

主机侧可设置容器共享持久化使能状态。若持久化功能为开启状态，则重启系统后设备的容器共享模式使能状态与重启前保持一致。

```shell
npu-smi set -t device-share-cfg-recover -d ${value}
```

**表 5 参数说明**

|参数|参数选项|说明|
|:---|:---|:---|
|value|<ul>默认值：禁用<li>禁用(0)</li><li>使能(1)</li></ul>|容器共享持久化使能状态。|

（可选）针对A3 推理系列产品，如采用下文提到的docker方式部署，则在主机侧需要通过`npu-smi`工具开启支持单Die通入容器的模式。

```shell
npu-smi set -t multi-die-policy -d ${value}
```

**表 6 参数说明**

|参数|参数选项|说明|
|:---|:---|:---|
|value|<ul>默认值：双Die联合模式<li>双Die联合模式(0)</li><li>单Die独立模式(1)</li></ul>|A3的device通入容器的策略模式|

### 虚拟化环境检测工具准备

  用户需要保证业务容器`/usr/bin`目录内包含`systemd-detect-virt`命令行工具，该工具用于检测系统的运行环境是否为虚拟化环境和虚拟化方式。若容器内未安装systemd-detect-virt工具，软切分动态库在调用dcmi接口时会出现故障。

## 源码获取

可以使用如下方式获取`vCANN-RT`源码。

```shell
git clone <ubs-virt-enpu-vcann-rt-url>
```

### 源码目录结构

`vCANN-RT`源码的主要目录结构如下所示：

```shell
.
├── scripts   // 存放项目中编译构建使用的脚本文件
├── src       // 存放项目的功能实现源码，仅该目录参与构建出包
└── test      // 存放项目的ut用例
```

## 编译

在编译vCANN-RT源码前，用户需要设置CANN的环境变量。如果驱动安装在非默认路径，还需要设置驱动路径环境变量。

`vCANN-RT`在代码仓中提供了统一的编译构建脚本（即`make_build.sh`文件），可以直接执行该脚本文件进行编译构建。

```shell
bash make_build.sh
```

编译完成之后，会在`build`目录下面产生相应的编译产物。

## 部署

用户可以选择将软切分动态库`libvruntime.so`和检测工具`enpu-monitor`分别拷贝到`/opt/enpu/vcann-rt/lib`和`/opt/enpu/vcann-rt/tools`目录。（如果选择不拷贝，则需要在后面的流程中修改相应的路径。）

### 配置preload文件
  <span id="step1"></span>
  创建ld.so.preload文件，并将libvruntime.so的路径配置到ld.so.preload文件中，用于预加载软切分动态库。

  ```shell
  vi ld.so.preload
  # 例如libvruntime.so 的路径为/opt/enpu/vcann-rt/lib
  /opt/enpu/vcann-rt/lib/libvruntime.so
  ```

  主机侧ld.so.preload文件的路径用户可自定义，文档后续内容中使用${preload_path}表示，容器内为固定路径`/etc/ld.so.preload`。不建议将ld.so.preload文件放置在主机的`/etc`目录，否则将在主机侧预加载软切分动态库，可能影响主机侧业务。

### 启动业务容器

vCANN-RT支持两种方式启动业务容器：

#### 方式一：基于kubernetes编排系统部署

  用户通过k8s yaml文件，在申请容器时声明所需的vNPU算力资源百分比例和显存资源数量。k8s会将容器调度到资源充足的节点，并将算力和显存资源配额等信息以配置文件挂载到容器内。同时，k8s会将vCANN-RT的包挂载到容器，容器内即可使能vCANN-RT软切分能力。

  对于Atlas A2 / A3 推理系列产品，yaml配置文件的格式可参考如下示例文件vnpu-base.yaml：

  ```shell
    apiVersion: mindxdl.gitee.com/v1
    kind: AscendJob
    metadata:
        # 容器名, 建议与yaml文件名一致
        name: vnpu-base 
        # namespace
        namespace: vnpu 
        labels:
            framework: pytorch
            ring-controller.atlas: ascend-910b
            fault-scheduling: "force"
            fault-retry-times: "10"
            # 算力aicore配额，单位：%
            huawei.com/scheduler.softShareDev.aicoreQuota: "20" 
            # 显存HBM配额，单位：MB
            huawei.com/scheduler.softShareDev.hbmQuota: "60000" 
            # 调度策略，默认弹性模式(elastic)，可选fixed-share，elastic，best-effort
            huawei.com/scheduler.softShareDev.policy: "elastic" 
        annotations:
            # 集群调度组件的调度策略
            huawei.com/schedule_policy: "chip1-softShareDev"
    spec:
        # work when enableGangScheduling is true
        schedulerName: volcano 
        runPolicy:
            # work when enableGangScheduling is true
            schedulingPolicy: 
                # soft share dev tasks with multiple replicas are only supported for single-machine scenarios.
                minAvailable: 2
                queue: default
        successPolicy: AllWorkers
        replicaSpecs:
            Master:
                replicas: 1
                restartPolicy: Never
                template:
                    metadata:
                      labels:
                        ring-controller.atlas: ascend-910b
                    spec:
                        automountServiceAccountToken: false
                        nodeSelector:
                            # 系统架构：huawei-arm / huawei-x86
                            host-arch: huawei-arm
                            # depend on your device model, 910bx8 is module-910b-8 ,910bx16 is module-910b-16
                            accelerator-type: module-910b-8
                        containers:
                        # do not modify
                        - name: ascend 
                          # 镜像名称
                          image: ${image_name} 
                          imagePullPolicy: IfNotPresent
                          env:
                            # IP address of the physical node, which is used to identify the node where the pod is running
                            - name: XDL_IP 
                              valueFrom:
                                fieldRef:
                                    fieldPath: status.hostIP
                          command:
                            - /bin/bash
                            - -c
                          args: [ "sleep 3000000;" ]
                          ports: 
                            # default value containerPort: 2222 name: ascendjob-port if not set
                            # determined by user
                            - containerPort: 2222
                              # do not modify 
                              name: ascendjob-port 
                          resources:
                            requests:
                                # should be equal to huawei.com/scheduler.softShareDev.aicoreQuota
                                huawei.com/Ascend910: 20
                            limits:
                                # should be equal to huawei.com/scheduler.softShareDev.aicoreQuota
                                huawei.com/Ascend910: 20
                          volumeMounts:
                          - name: sbin
                            mountPath: /usr/local/sbin/
                          - name: ascend-driver
                            mountPath: /usr/local/Ascend/driver
                          - name: dshm
                            mountPath: /dev/shm
                          - name: localtime
                            mountPath: /etc/localtime
                            # 软切分动态库路径
                          - name: libpreload 
                            mountPath: /opt/enpu/vcann-rt/lib/libvruntime.so
                            # 监测工具路径
                          - name: tools
                            mountPath: /opt/enpu/vcann-rt/tools/enpu-monitor
                            # preload配置文件路径
                          - name: preload 
                            mountPath: /etc/ld.so.preload
                        volumes: 
                        - name: sbin
                          hostPath: 
                            path: /usr/local/sbin/
                        - name: ascend-driver
                          hostPath:
                            path: /usr/local/Ascend/driver
                        - name: dshm
                          emptyDir:
                            medium: Memory
                        - name: localtime
                          hostPath:
                            path: /etc/localtime
                          # 软切分动态库路径
                        - name: libpreload 
                          hostPath:
                            path: /opt/enpu/vcann-rt/lib/libvruntime.so
                          # 监测工具路径
                        - name: tools 
                          hostPath:
                            path: /opt/enpu/vcann-rt/tools/enpu-monitor
                          # preload配置文件路径
                        - name: preload 
                          hostPath:
                            path: ${preload_path}/ld.so.preload
            Worker:
              replicas: 1
              restartPolicy: Never
              template:
                metadata:
                  labels:
                    ring-controller.atlas: ascend-910b
                spec:
                  automountServiceAccountToken: false
                  nodeSelector:
                    # 系统架构：huawei-arm / huawei-x86
                    host-arch: huawei-arm
                    # depend on your device model, 910bx8 is module-910b-8 ,910bx16 is module-910b-16
                    accelerator-type: module-910b-8 
                  containers:
                    # do not modify
                    - name: ascend 
                      image: ${image_name}
                      imagePullPolicy: IfNotPresent
                      env:
                        - name: XDL_IP 
                          valueFrom:
                            fieldRef:
                              fieldPath: status.hostIP
                      command: 
                        - /bin/bash
                        - -c
                      args: [ "sleep 3000000;" ]
                      ports: 
                        - containerPort: 2222
                          # do not modify         
                          name: ascendjob-port        
                      resources:
                        requests:
                          # should be equal to huawei.com/scheduler.softShareDev.aicoreQuota
                          huawei.com/Ascend910: 20 
                        limits:
                          # should be equal to huawei.com/scheduler.softShareDev.aicoreQuota
                          huawei.com/Ascend910: 20 
                      volumeMounts:
                        - name: sbin
                          mountPath: /usr/local/sbin/
                        - name: ascend-driver
                          mountPath: /usr/local/Ascend/driver
                        - name: dshm
                          mountPath: /dev/shm
                        - name: localtime
                          mountPath: /etc/localtime
                        - name: libpreload 
                          mountPath: /opt/enpu/vcann-rt/lib/libvruntime.so
                        - name: tools
                          mountPath: /opt/enpu/vcann-rt/tools/enpu-monitor
                        - name: preload 
                          mountPath: /etc/ld.so.preload
                  volumes:
                    - name: sbin
                      hostPath: 
                        path: /usr/local/sbin/
                    - name: ascend-driver
                      hostPath:
                        path: /usr/local/Ascend/driver
                    - name: dshm
                      emptyDir:
                        medium: Memory
                    - name: localtime
                      hostPath:
                        path: /etc/localtime
                    - name: libpreload 
                      hostPath:
                        path: /opt/enpu/vcann-rt/lib/libvruntime.so
                    - name: tools 
                      hostPath:
                        path: /opt/enpu/vcann-rt/tools/enpu-monitor
                    - name: preload 
                      hostPath:
                        path: ${preload_path}/ld.so.preload
  ```

  可根据实际业务进行参数配置：
  - metadata:
      - name: 容器名,建议与yaml文件名一致。
      - namespace: 命名空间。
      - labels:
          - huawei.com/scheduler.softShareDev.aicoreQuota: 算力aicore配额（需配置为整数），单位：%
          - huawei.com/scheduler.softShareDev.hbmQuota: 显存HBM配额（需配置为整数），单位：MB
          - huawei.com/scheduler.softShareDev.policy: 调度策略，默认为弹性模式。
            - 固定配额模式（fixed-share）
            - 弹性模式（elastic）
            - 争抢模式（best-effort）
            
          - 每种模式的详细介绍参见[表7](#table7)，其中在争抢模式下，为充分利用算力资源，此时aicore的使用将不受配额的限制，但 HBM 的使用仍受配额的限制。

  - host-arch: 系统架构 huawei-arm / huawei-x86
  - containers:
      - image: 镜像名。
  - volumes:
      - name: libpreload 
          - hostPath:
              - path: /opt/enpu/vcann-rt/lib/libvruntime.so # 软切分动态库路径，根据实际路径进行修改
      - name: tools 
          - hostPath:
              - path: /opt/enpu/vcann-rt/tools/enpu-monitor # 检测工具路径，根据实际路径进行修改
      - name: preload # preload配置文件路径。
          - hostPath:
              - path: ${preload_path}/ld.so.preload # [步骤1](#step1)中创建的ld.so.preload文件路径。

  对于Atlas 350 加速卡产品，yaml配置文件的格式需要做以下修改：

  1. 删除910b相关配置：ring-controller.atlas: ascend-910b 及 accelerator-type: module-910b-8

  2. 节点资源配置参数名修改为huawei.com/npu，示例如下

      ```
      resources:
          requests:
              # should be equal to huawei.com/scheduler.softShareDev.aicoreQuota
              huawei.com/npu: 20
          limits:
              # should be equal to huawei.com/scheduler.softShareDev.aicoreQuota
              huawei.com/npu: 20
      ```
    

  **表 7 调度模式介绍**<a id="table7"></a>

  |模式名称|特点描述|
  |:---|:---|
  |固定配额模式（fixed-share）|各vNPU按配比严格执行时间片。<br> a. 若有暂未分配的vNPU资源，则在最后一个vNPU时间片消耗完时，睡眠一段时间。此时NPU将闲置空转。<br> b. 某vNPU上无任务运行，仍会按照给定比例消耗完该vNPU的时间片。|
  |弹性模式（elastic）| 由于固定配额模式可能出现NPU资源浪费，在此模式下，在各vNPU按配比执行时间片的基础上，为提高卡资源利用率，优化了调度逻辑。<br> a. 最后一个vNPU时间片消耗完，马上将NPU使用权释放给下一个vNPU，跳过睡眠逻辑。 <br> b. 在某vNPU上无任务运行，则直接跳过未消耗完的时间片，切换至另一个vNPU。<br> c. 时间片借用机制：正在执行的vNPU在识别到卡资源空闲的情况下，允许其他vNPU借用该时间片内浪费的资源，使卡上资源不被浪费，进而提升整卡利用率。|
  |争抢模式（best-effort）|vNPU之间自行争抢NPU资源，该模式的资源利用率最高，但无法保证vNPU的QoS。|

  使用yaml文件拉起容器：

  ```shell
  kubectl apply -f ${container_name.yaml}
  # 查看容器
  kubectl get pods -n ${namespace}
  ```

  如果容器启动失败，可参考方式二中的解决方法。

  进入容器：

  ```shell
  kubectl exec -it ${pod_name} -n ${namespace} bash
  ```

#### 方式二：docker方式部署（不依赖kubernetes组件）

当不依赖k8s组件使用vCANN-RT时，需要用户在启动容器时自行挂载软切分相关动态库、文件和设备。例如软件分动态库、配置文件、共享内存设备和物理NPU设备。具体步骤如下：

1. 创建vNPU配置文件和共享内存设备。

    针对每个容器，需要在主机侧创建一个配置文件，并映射到容器的npu_info.config文件中（由于不同容器的配置内容不同，需要确保每个容器的配置文件在主机侧独立存储并明确区分，比如通过文件名或者路径区分）。配置文件的格式和字段示例如下：

    ```bash
      physical-npu-id=0
      virtual-npu-id=0
      aicore-quota=20
      memory-quota=1024
      shm-id=xxx
      scheduling-policy=2
    ```

    **表 8 配置项说明**

    |参数|参数选项|说明|
    |:---|:---|:---|
    |physical-npu-id|物理NPU id|`physical-npu-id=0`表示使用第0张物理NPU。|
    |virtual-npu-id|vNPU id|需要从0开始配置，并且同一个物理NPU下的vNPU不允许重复。|
    |aicore-quota|AI Core资源配额，单位为%|表示算力使用的时间比例，需配置为整数。假设当前每个time slice为100ms, 申请了20%的算力资源，那么该容器有20ms的NPU使用权。|
    |memory-quota|HBM资源配额，单位为MB|表示显存资源使用容量，需配置为整数。当前容器内所有进程使用的HBM总量不能超过HBM资源配额。|
    |shm-id|共享内存文件名称|该文件名称采用物理NPU对应的VDie ID, 可以保证全局唯一。<br>通过`npu-smi info -t board -i ${id} -c ${chip_id}`命令查询VDie ID。<br>查询完成之后，可以通过`-`符号拼接成文件名称，例如：`shm-id=11111111-22222222-33333333-44444444-55555555` |
    |scheduling-policy|<ul>默认配置为2。<li>1: 固定配额模式（fixed-share）</li><li>2: 弹性模式（elastic）</li><li>3: 争抢模式（best-effort）</li></ul>|调度策略（具体介绍可参见[表7](#table7)，其中在争抢模式下，为充分利用算力资源，此时aicore的使用将不受配额的限制，但 HBM 的使用仍受配额的限制）。|

    此外，需要设置配置文件具有合适的权限，建议为644。

2. 启动业务容器。

    用户在启动容器时，需要将软切分相关动态库、文件和设备挂载到容器(具体可参见[表9](#table9))，容器启动命令可参考(假如使用第0张NPU卡)：

    ```bash
      docker run -it --name=${container_name} \
      --device=/dev/davinci0:/dev/davinci0 \
      --device=/dev/davinci_manager \
      --device=/dev/hisi_hdc:/dev/hisi_hdc \
      -v /usr/local/sbin:/usr/local/sbin \
      -v /usr/local/Ascend/driver:/usr/local/Ascend/driver \
      -v /dev/shm:/dev/shm \
      -v /opt/enpu/vcann-rt/lib/libvruntime.so:/opt/enpu/vcann-rt/lib/libvruntime.so \
      -v /opt/enpu/vcann-rt/tools/enpu-monitor:/opt/enpu/vcann-rt/tools/enpu-monitor \
      -v /xxx/npu_info.config:/etc/enpu/vcann-rt/npu_info.config \
      -v ${preload_path}/ld.so.preload:/etc/ld.so.preload \
      ${image_name} /bin/bash
    ```

    如果遇到容器启动报错`libboundscheck.so: cannot open shared object file: No such file or directory`或者`GLIBC_xxx not found`。解决办法：

      1. 取消容器启动命令中ld.so.preload文件的映射，并重新启动容器。

      2. 参照文末FAQ中对应的步骤执行。

      3. 执行`export LD_PRELOAD=/opt/enpu/vcann-rt/lib/libvruntime.so`命令加载动态库。

    **表 9 参数说明**<a id="table9"></a>

    |文件/工具|路径|
    |:---|:---|
    |软切分动态库|主机侧和容器内均为建议路径：`/opt/enpu/vcann-rt/lib/libvruntime.so`|
    |监测工具|主机侧和容器内均为建议路径：`/opt/enpu/vcann-rt/tools/enpu-monitor`|
    |物理NPU设备|主机侧和容器内均为固定路径：`/dev/davinci0`|
    |共享内存设备|主机侧和容器内均为固定路径：`/dev/shm`|
    |配置文件|<ul><li>主机侧可存放在自定义路径。</li><li>容器内为固定路径：`/etc/enpu/vcann-rt/npu_info.config`</li></ul>|
    |预加载动态库文件|主机侧可存放在自定义路径，容器内为固定路径：`/etc/ld.so.preload`。不建议将`ld.so.preload`文件放置在主机的`/etc`目录，否则将在主机侧预加载软切分动态库，可能影响主机侧业务。|


### 启动业务，使用vCANN-RT服务

  - 拉起容器之后，可通过环境变量`ENPU_LOG_LEVEL`配置日志级别。例如：

    ```bash
    export ENPU_LOG_LEVEL=3
    ```
    日志级别由高到低分别是FATAL(0), ERROR(1), WARN(2), INFO(3), DEBUG(4)。默认日志级别为INFO，设置日志级别为DEBUG后，可在日志文件（日志文件路径：`/var/log/enpu/vcann-rt/`）中查看DEBUG日志，屏显仅打印INFO及以上级别日志。

  - 推理任务启动时，会自动拉起vCANN-RT服务进行算力控制和显存控制，软切分服务启动成功之后会设置一个进程级环境变量`ENPU_ENABLE=True`。如果日志回显内容为`"Successfully to initialize all module."`, 则表示vCANN-RT服务启动成功。

  - 在容器内可查询配置文件获取vNPU资源配额等信息：

    ```shell
    cat /etc/enpu/vcann-rt/npu_info.config
    ```

  - 在容器内可通过监测工具查询vNPU资源配额和内存使用情况等信息。

    ```bash
    /opt/enpu/vcann-rt/tools/enpu-monitor
    ```

### 环境变量汇总

**表 10 环境变量列表**

| 环境变量 | 范围 | 默认值 | 说明 |
|-|-|-|-|
| `ASCEND_HOME_PATH` | 编译 | `/usr/local/Ascend/cann` | CANN安装路径，可通过 `source /path/to/cann/set_env.sh` 设置。 |
| `ENPU_ASCEND_DRIVER_PATH` | 编译 | `/usr/local/Ascend` | HDK driver安装路径。 |
| `ENPU_LOG_LEVEL` | 运行 | `3` | 日志级别，FATAL(0), ERROR(1), WARN(2), INFO(3), DEBUG(4)。 |
| `ENPU_ENABLE` | 运行 | `True` | vCANN-RT启动成功之后设置的进程级环境变量。 |

## 约束

- 由于vCANN-RT解决方案使用了共享内存，因此用户需要确保在可信用户范围内使用。
- 由于硬件设备的限制(可以参考昇腾社区[使用约束](https://www.hiascend.com/document/detail/zh/canncommercial/850/appdevg/acldevg/aclcppdevg_000222.html))，建议vCANN-RT最大切分数量不超过单个device支持的最大用户进程数。
- 用户对aicore算力资源的配额设置不能超过100%，对HBM显存资源的配额设置不能超过当前物理卡的可用显存资源总量，否则可能在容器拉起或者业务运行时出现异常。例如：HBM配额设置超限之后，可能会在业务运行时出现OOM异常报错。
- 采用docker方式部署时，用户需要保证同一个device设置为同一种调度策略，否则可能会导致业务运行异常。
- 若用户通过源码自行编译软切分动态库，则需要保证主机侧的CANN版本和容器镜像中的CANN版本保持一致。
- 建议通过k8s的部署方式使用软切分能力，由MindCluster统一提供资源调度和管理能力。
- 若不具备k8s部署条件，可以通过docker部署方式使用软切分能力，但需要管理员统一规划docker容器aicore和HBM的配置总和不超过资源总量，且单device仅允许配置单种调度策略。如若未按照上述约束配置，可能出现以下业务影响:
  - 若多容器配置的aicore总量超出100%， 各容器无法按照原先设定的资源使用。
  - 若多容器配置的HBM总量超出当前单Device可用内存大小，某一容器内的业务运行时，可能因为实际内存不足，导致报错OOM内存不足。
  - 若单卡上各容器配置的调度策略不相同，各容器无法按照原先设定的资源使用，建议各容器配置的调度策略相同。
- 当前vCANN-RT方案适配CANN软件版本为商发版本9.1.0，由于版本限制，暂时不支持persistent task的使用。

## FAQ

1. hook拦截runtime API提示`can't find function`:

    当前vCANN-RT方案适配CANN软件版本为商发版本9.1.0，部分runtime API在CANN其他版本未支持。

2. 容器启动或者业务运行时报错`GLIBC_xxx not found`:
    
    由于GLIBC兼容性问题，运行环境和编译环境的GLIBC版本不兼容，建议运行环境的版本大于等于编译环境，或者在容器内编译。

3. 容器启动或者业务运行时报错`libboundscheck.so: cannot open shared object file: No such file or directory`：

    由于软切分动态库在运行时依赖安全函数库，因此用户需要确保容器中存在安全函数库并能够被正常链接，可以参考 [https://gitcode.com/openeuler/libboundscheck](https://gitcode.com/openeuler/libboundscheck) 完成构建部署。

4. 容器内模型业务运行时报错`[SqCqManage] Alloc sq cq fail......`:
    
    由于模型在推理框架中运行时，推理框架使用的stream流资源过多，超出了NPU硬件资源的限制导致报错。