# ubs-virt-ovs

## Introduce

`ubs-virt-ovs` is ubs-virt Provides network capabilities and supports configuring bandwidth for specified urma devices. 

## Download source code

You can download the `ubs-virt-ovs` source code using the following two methods.

```shell
# method one
$ git clone <ubs-virt-ovs-repo-url>
$ git submodule update --init --recursive
# method two
$ git clone <ubs-virt-ovs-repo-url> --recurse-submodules
```

## source code directory structure

`ubs-virt-ovs` main directory structure of the source code is shown below.

```shell
.
├── build     // Stores script files used for compilation and building in the project.
├── sdk       // Stores the SDK code that is exposed to the outside world.
├── src       // This directory contains the source code for the project's functionalities; only this directory participates in the build process.
└── test      // Ut and dtfuzz, etc., are used to store projects.
```

## build

`ubs-virt-ovs` provides a unified build script (i.e., `build.sh`) in the code repository, which can be executed directly to compile and build. No configuration is required by default; simply execute the script.
The compiled output is located in the `build/output` directory.

```shell
$ ./build/build.sh
[INFO] Build type: RelwithDebinfo
[INFO] Project root: /tmp/xxx
[INFO] Spec file: /tmp/xxx/ubs-virt-ovs/build/ubs-virt-ovs.spec
[INFO] Source tar created: /tmp/xxx/ubs-virt-ovs/build/rpmbuild/SOURCES/ubs-virt-ovs-1.0.0.tar.gz
...
[INFO] Outpud RPMs:
total 716K
-rw-------. 1 root root  50K ubs-virt-ovs-1.0.0-1.rel.aarch64.rpm
-rw-------. 1 root root 640K ubs-virt-ovs-debuginfo-1.0.0-1.rel.aarch64.rpm
-rw-------. 1 root root  23K ubs-virt-ovs-debugsource-1.0.0-1.rel.aarch64.rpm
```

## deployment

Upload the compiled output to the environment and install it using RPM.

```shell
$ rpm -ivh ubs-virt-ovs-*
```
