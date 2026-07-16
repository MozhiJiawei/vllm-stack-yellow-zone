# UBS virt-optimizer 安装指南

# 部署说明

## 环境要求

### 获取软件包

ubs-optimizer的发布件如下：

| RPM包                                             | 说明                                      |
|:-------------------------------------------------|:----------------------------------------|
| ubs-optimizer-\<version>.aarch64.rpm  | 主程序包，包含配置，采集工具，优化分析工具等。构建软件包参考[构建指导](../../../virt-optimizer/docs/build_install/构建指导.md) |

### 系统要求

- 场景一：

   |项目|版本信息|
   |:----|:----|
   |架构|ARM架构，鲲鹏型号CPU，昇腾型号NPU|
   |硬件|Atlas 900 A3 SuperPoD 超节点A900；A3 SuperPoD 超节点；Atlas 800T A2 训练服务器；A800T A2 训练服务器|
   |操作系统|openEuler 22.03 LTS SP3|
   |CPU架构|aarch64|
   |用户权限|安装与管理需 `root` 权限|

- 场景二：

   |项目|版本信息|
   |:----|:----|
   |架构|X86架构，昇腾型号NPU|
   |硬件|G8600服务器|
   |操作系统|TencentOS Server 3.1|
   |CPU架构|x86|
   |用户权限|安装与管理需 `root` 权限|

## 软件安装

> 宿主机和虚拟机均需要安装 `ubs-optimizer` RPM 包。  
> 安装 RPM 包前，请确保操作系统已加载 `vsock` 内核驱动：
>
> ```bash
> modprobe vsock
> ```

1. 在宿主机安装ubs-opt性能优化工具

   ```bash
   rpm -ivh ubs-optimizer-<version>.rpm
   ```

2. 物理机安装完成后，在虚拟机安装ubs-opt性能优化工具

   ```bash
   rpm -ivh ubs-optimizer-<version>.rpm
   ```

   命令回显示例如下：

   ```terminal_output
   rpm -ivh virt-optimizer-0.1.0-k5.1-aarch64.rpm 
   Verifying...                          ################################# [100%]
   Preparing...                          ################################# [100%]
   Updating / installing...
      1:virt-optimizer-0.1.0-k5.1        ################################# [100%]
   ```

### 安装结果

|路径                                     |用途         |
|:----------------------------------------|:-----------|
| /usr/local/sbin/ubs-opt        | 性能数据采集        |
| /usr/local/sbin/ubs-opt-guard | 性能数据处理        |
| /usr/local/sbin/ubs-opt-tuner       | 优化分析 |
| /usr/local/sbin/ubs-optimizer/config.json  | 配置文件 |

### ubs-optimizer进程管理

- 启动进程

   - 虚拟机内启动采集进程：

      ```bash
      ubs-opt start_ebpf
      ```

   - 物理机内启动性能分析：

      ```bash
      ubs-opt-tuner start
      ```

- 停止进程

   虚拟机内停止采集进程：

   ```bash
    ubs-opt stop_ebpf
   ```

### 软件卸载

- 停止进程

   虚拟机内执行：

   ```bash
    ubs-opt stop_ebpf
   ```

- 卸载软件

   - 虚拟机内执行
   
      ```bash
      rpm -e virt-optimizer
      ```

   - 物理机内执行

      ```bash
      rpm -e virt-optimizer
      ```
