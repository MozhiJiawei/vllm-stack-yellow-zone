# UBS Optimizer (UB Service Optimizer)

`UBS Optimizer`（UB Service Optimizer, 简称UBS Optimizer）是昇腾虚拟化场景下针对虚拟机性能优化的调优工具。使用该工具可实现昇腾虚拟化场景下推理性能瓶颈识别和调优下发的功能。

---

## 📌 快速开始

### 环境要求 & RPM包构建 & 安装部署

- 适用场景：详见 [性能优化方法](docs/optimize_operations/性能优化方法.md)
- 构建依赖及RPM包构建：详见 [构建指导](docs/build_install/构建指导.md)
- 安装部署：详见 [部署说明](docs/build_install/部署说明.md)

---

## 🧩 核心功能

- **采集内核事件性能数据**
- **识别昇腾推理场景下虚拟机性能瓶颈**
- **基于瓶颈提供调优建议和调优项说明**

> 详细功能说明请参阅[架构设计](docs/design/架构设计.md)。

## 📂 项目结构概览

```text
## 📌 快速开始
virt-optimizer/
├── ebpf/                       # ubs_optimizer 核心服务
│   ├── 3rdparty/               # 第三方依赖
│   ├── src/                    
│   │   ├── client/             # 采集模块
│   │   ├── common/             # 通用模块
│   │   ├── optimizer/          # 优化器相关模块
│   │   └── server/             # 数据处理模块
│   └── tests/                  # 测试用例
├── docs/                       # 文档
├── build.sh                    # 构建脚本
├── build_rpm.sh                # 构建 RPM 子脚本
└── Readme                      # 项目说明文件
```

---

## 🧪 开发者测试

项目包含完整的单元测试（UT）：
详见 [单元测试开发指南](docs/test/单元测试开发指南.md)

---

## 📚 文档索引

所有技术文档位于 [`docs/`](./docs/) 目录，包括：

- **架构设计**：[架构设计.md](./docs/design)
- **构建指导**：[构建指导.md](./docs/build_install/构建指导.md)
- **部署说明**：[安装部署.md](./docs/build_install/部署说明.md)
- **配置说明**：[配置说明.md](./docs/config/配置说明.md)
- **场景示例**：[最佳实践.md](./docs/example/最佳实践：如何使用ubs-optimizer.md)
- **测试指南**：[单元测试开发指南.md](./docs/test/单元测试开发指南.md)
- **性能优化方法**：[性能优化方法.md](./docs/optimize_operations/性能优化方法.md)
- **安全声明**：[安全声明.md](./docs/security/安全声明.md)

---

> 项目主页：[https://gitcode.com/openeuler/ubs-virt/tree/master](./)
