# UBS Optimizer (UB Service Optimizer)

UB Service Optimizer (`UBS Optimizer`) is designed to optimize virtual machine performance in Ascend-based virtualization scenarios. This tool can be used to identify inference performance bottlenecks and deliver optimization policies in these scenarios.

---

## 📌 Quick Start

### Environment Requirements & RPM Package Building & Installation and Deployment

- Application scenarios: For details, see [Performance Optimization Methods](docs/optimize_operations/性能优化方法.md).
- Build dependencies and RPM package building: For details, see [Build Guide](docs/build_install/构建指导.md).
- Installation and deployment: For details, see [Deployment Description](docs/build_install/部署说明.md).

---

## 🧩 Core Functions

- **Collects kernel event performance data**
- **Identifies virtual machine performance bottlenecks in Ascend-based inference scenarios**
- **Provides optimization suggestions with corresponding descriptions based on bottlenecks**

> For details about the functions, see [Architecture Design](docs/design/架构设计.md).

## 📂 Project Structure

```text
## 📌 Quick Start
virt-optimizer/
├── ebpf/                       # ubs_optimizer core service
│   ├── 3rdparty/               # Third-party dependencies
│   ├── src/                    
│   │   ├── client/             # Collection module
│   │   ├── common/             # Common modules
│   │   ├── optimizer/          # Optimizer-related modules
│   │   └── server/             # Data processing module
│   └── tests/                  # Test cases
├── docs/                       # Documents
├── build.sh                    # Build script
├── build_rpm.sh                # RPM build sub-script
└── Readme                      # Project description file
```

---

## 🧪 Developer Testing

The project contains complete unit tests (UTs).
For details, see [Unit Test Development Guide](docs/test/单元测试开发指南.md).

---

## 📚 Documentation Index

All technical documents are located in the [`docs/`](./docs) directory, including:

- **Architecture design**: [Architecture Design.md](./docs/design)
- **Build guide**: [Build Guide.md](./docs/build_install/构建指导.md)
- **Deployment description**: [Installation and Deployment.md](./docs/build_install/部署说明.md)
- **Configuration description**: [Configuration Description.md](./docs/config/配置说明.md)
- **Scenario example**: [Best Practices.md](./docs/example/最佳实践：如何使用ubs-optimizer.md)
- **Test guide**: [Unit Test Development Guide.md](./docs/test/单元测试开发指南.md)
- **Performance optimization methods**: [Performance Optimization Methods.md](./docs/optimize_operations/性能优化方法.md)
- **Security statement**: [Security Statement.md](./docs/security/安全声明.md)

---

> Project homepage: [https://gitcode.com/openeuler/ubs-virt/tree/master](./)
