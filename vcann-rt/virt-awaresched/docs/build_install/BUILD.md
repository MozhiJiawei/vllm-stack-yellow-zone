# 构建指导

## 拉取源码

```shell
git clone https://gitcode.com/openeuler/ubs-virt.git
# 进入项目目录
cd virt-awaresched
```

## 构建工具链准备

```shell
sudo yum install gcc-c++ gcc cmake make -y
```

## 构建

### 构建依赖

virt-awaresched构建依赖信息已经记录在spec文件（virt-awaresched.spec）中。

具体内容如下：

```shell
Requires: patch libvirt-devel libboundscheck
```

### 安装依赖

yum命令手动安装

```shell
sudo yum install patch libvirt-devel libboundscheck -y
```

### 执行构建

- 执行Release构建（无调试信息），产物输出到项目顶层目录的build/目录下。

    ```shell
    bash build.sh
    ```

- 执行Debug构建（包含调试信息），产物输出到项目顶层目录的build/目录下。

    ```shell
    bash build.sh -D
    ```

## 生成rpm包

构建项目，并打包成RPM文件输出到项目顶层目录下得output/目录下。

```shell
bash build.sh package
```

产物如下：

```filepath
└── output                                                            # 打包输出文件目录
    └── virt-awaresched-1.0.0-1.aarch64.rpm                           # virt-awaresched rpm安装包
```
