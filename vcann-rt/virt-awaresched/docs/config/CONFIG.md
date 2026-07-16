# vas 配置说明

> vas-daemon服务启动参数, 编辑`/usr/lib/systemd/system/vas-daemon.service`文件中，启动命令行。

| key                  | 说明                                                 | 参数类型      | 合法范围                                                                                   | 默认值      |
|:---------------------|:---------------------------------------------------|:----------|:---------------------------------------------------------------------------------------|:---------|
| smt                  | cpu配分配粒度                                           | bool      | <ul><li>true：按照超线程分配。</li><li>false：按照物理核分配。</li></ul>                                 | true     |
| sched-policy         | 调度策略                                               | string 枚举 | <ul><li>dynamicAffinity：vcpu动态绑核，依赖内核启动参数配置动态绑核特性开启。</li><li> affinity：静态绑核。</li></ul> | affinity |
| dynamic-util-thresh  | 动态亲和策略的cpu使用率阈值，cpu负载超过阈值，cpu动态绑核                | int       | (0, 100)                                                                               | 85       |
| skip-cluster-cpumask | 为vcpu分配cpu时将忽略skip-cluster-cpumask对应的cpu所在的Cluster | string    | 支持范围指定, 例如: 0-10 <br /> 支持分段指定: 0-1,2,3-4                                              | -        |
| range-affinity       | 重调度的虚拟机，是否包含非numa范围绑核的虚拟机                         | bool      | <ul><li>true: 允许非numa范围绑核的虚拟机参与调度。</li><li>false: 仅允许numa范围绑核的虚拟机参与调度。</li></ul>       | true     |
