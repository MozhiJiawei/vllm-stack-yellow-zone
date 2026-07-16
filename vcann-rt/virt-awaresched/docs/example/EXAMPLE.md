# 典型使用场景

> 下文中, 使用到的CLI指令, 可以参考[CLI 指令参考](../cli/cli_docs_reference.md)。

---

## 自动调优

1. 参考[部署文档](../build_install), 服务启动后, 会进行自动调优。
2. 执行以下命令，查询调度结果。
    
    ```bash
       vasctl query affinity --scope all 
    ```
    
    调度结果如下：

    ![自动调度](images/自动调度.png "调度结果示例")

---

## 手动调度

如果对当前自动调度结果不满意, 可以尝试使用以下指令, 手动重新调度指定的虚拟机。

```shell
vasctl opt reassign --scope VM1
```

![手动调度](images/手动调度.png "手动调度示例")

---

## 恢复

一般情况下, 服务正常退出时, 会回滚所有调优操作。当服务异常退出, 无法正确恢复调优操作时, 可以使用以下指令, 手动恢复。

```bash
vasctl opt recover
```
示例信息如下：
![手动恢复](images/手动恢复.png "手动恢复示例")
