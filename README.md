# coke-postgres

**coke-postgres** 是 [wf-postgres](https://github.com/tenire/wf-postgres) 的 C++20 协程包装器，基于 [coke](https://github.com/kedixa/coke) 开发。

它提供了一层轻量级的接口封装，使其能够使用 `co_await` 来进行异步的 PostgreSQL 操作，并包含对逻辑事务连接的基础管理。

## ✨ 特性 (Features)

- **C++20 协程接口**：包装了 `wf_postgres` 的底层接口，提供协程 `co_await` 支持。
- **逻辑连接模型**：使用 `transaction` URI 参数和无状态对象设计，配合底层 Factory 管理固定会话（Session）。
- **安全的视图封装**：封装了 Result View，包含基础的空指针防备机制。

## 📦 依赖 (Dependencies)

本项目使用 [xmake](https://xmake.io/) 作为默认构建系统，底层依赖以下核心组件：

- [workflow](https://github.com/sogou/workflow)：搜狗开源的 C++ 底层异步调度与网络通信框架。
- [coke](https://github.com/kedixa/coke)：针对 workflow 的 C++20 协程封装，提供对原生的 `co_await` 支持。
- [wf-postgres](https://github.com/tenire/wf-postgres)：基于 workflow 纯异步生态完整实现的 PostgreSQL 协议插件。
- **OpenSSL**：用于保障底层的 TLS 加密通信握手。

## 🚀 快速上手 (Quick Start)

### 1. 在项目中使用 / Use in your project

如果您的主项目使用 Xmake，建议通过 `package()` 直接引入本仓库，无需将其上传至 xrepo 即可直接复用：

```lua
-- xmake.lua
package("coke_postgres")
    add_deps("coke", "wf_postgres")
    add_urls("https://github.com/tenire/coke-postgres.git") -- 请替换为实际的仓库地址
    on_install("linux", "macosx", function (package)
        import("package.tools.xmake").install(package)
    end)
package_end()

add_requires("coke_postgres")

target("my_app")
    set_kind("binary")
    add_files("src/*.cc")
    add_packages("coke_postgres")
```


### 2. 本地编译与测试 / Local compile and run

本项目使用 `xmake` 构建，你可以选择开启自带的 Tutorial 示例或单元测试：

```bash
# 默认构建静态库
xmake f -c -y
xmake

# 开启教程和测试的构建
xmake f --tutorial=y --tests=y
xmake
```

### 基础用法示例

简单查询，不需要开启显式的连接会话即可独立发起：

```cpp
#include <iostream>
#include "ckpg/postgres.h"
#include "coke/wait.h"

coke::Task<int> hello_postgres(const ckpg::PostgresClientParams &params) {
    ckpg::PostgresClient cli(params);

    auto res = co_await cli.request("SELECT 'Hello, PostgreSQL!' AS greeting, current_timestamp;");
    
    if (res.state != coke::STATE_SUCCESS || res.resp == nullptr || res.resp->is_error()) {
        std::cerr << "Request failed or returned error.\n";
        co_return 1;
    }

    ckpg::PostgresResultSetView view(res.resp);
    std::vector<ckpg::PostgresCellView> cells;

    while (view.next_row(cells)) {
        for (const auto& c : cells) {
            std::cout << (c.is_null() ? "NULL" : c.as_string()) << "\t";
        }
        std::cout << "\n";
    }

    co_return 0;
}

int main() {
    ckpg::PostgresClientParams params;
    params.host = "127.0.0.1";
    params.port = 5432;
    params.username = "postgres";
    params.password = "mysecretpassword";
    
    return coke::sync_wait(hello_postgres(params));
}
```

### 事务级会话 (Transaction Session)

当需要执行 `BEGIN`、创建 `TEMP TABLE` 或者跨多条 SQL 的事务级隔离时，使用 `PostgresConnection` 获取逻辑单连：

```cpp
coke::Task<int> test_transaction(ckpg::PostgresClientParams params) {
    // 创建一个逻辑连接，独占底层一个 Session
    ckpg::PostgresConnection conn(params);

    // 发起连续的事务操作
    co_await conn.request("BEGIN");
    co_await conn.request("CREATE TEMP TABLE ckpg_tmp_test(v int)");
    co_await conn.request("INSERT INTO ckpg_tmp_test VALUES (42)");
    
    auto res = co_await conn.request("SELECT v FROM ckpg_tmp_test");
    ckpg::PostgresResultSetView view(res.resp);
    
    // ... 对数据进行处理 ...

    co_await conn.request("ROLLBACK");
    
    // 安全断开连接，及时向底层归还资源
    co_await conn.disconnect();
    
    co_return 0;
}
```

> **生命周期警告**：请始终保证 `PostgresConnection` 的析构晚于基于其发起的任何 `co_await conn.request()`。连接被析构时会同步释放其对应的**逻辑连接 ID**。

## 📜 许可证 (License)

本项目采用与 Coke / Workflow 一致的开源协议发行。
