# UBS virt-optimizer 命令行使用指南

## 启动optimizer优化器

```bash
ubs-opt-tuner start
```

> [!NOTE] 说明
>
> 将虚拟机性能数据拷贝到物理机对应路径后，执行该命令，ubs-optimizer会给出可配置优化项表格。

**示例**

将采集完成的虚拟机性能数据，从虚拟机拷贝到物理机时，执行以下操作获取可优化项：

```bash
ubs-opt-tuner start
```

回显的可配置优化项表格如下：

![img.png](./images/image-20253107.png "可配置优化项")

## 启动optimizer采集服务

```bash
ubs-opt start_ebpf
```

> [!NOTE] 说明
>
> 执行该命令后，ubs-optimizer会根据配置项，周期性采集虚拟机的性能数据，输出到`/var/ubs-opt/data/data.json`

## 停止optimizer采集服务

```bash
ubs-opt stop_ebpf
```

> [!NOTE] 说明
>
> 执行该命令后，ubs-optimizer将停止采集虚拟机性能数据。

## optimizer采集服务实践

1. 虚拟机内安装ubs-optimizer的组件完成后，执行以下命令启动ubs-optimizer服务。

    ```bash
    ubs-opt start_ebpf
    ```

2. 查看/var/ubs-opt/data/data.json：
![img.png](./images/image-20253108.png "data.json")

3. 采集完成后，执行以下命令停止ubs-optimizer服务。

    ```bash
    ubs-opt stop_ebpf
    ```
