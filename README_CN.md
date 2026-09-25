# coke-postgres

[English](README.md) | 简体中文

**coke-postgres** 是 [wf-postgres](https://github.com/tenire/wf-postgres) 的 C++20 协程包装器，基于 [coke](https://github.com/kedixa/coke) 开发。

它提供了一层高性能、类型安全且内存安全的协程接口封装，使开发者能够使用 `co_await` 进行异步 PostgreSQL 操作，原生支持参数化查询与连接会话/事务生命周期管理。

---

## ✨ 核心特性 (Features)

- **C++20 原生协程支持**：基于 `coke` 协程体系，完全废除回调地狱，支持原生 `co_await` 异步调用。
- **统一状态判定 (Unified Status)**：整合底层网络传输状态与 PostgreSQL 协议层 SQL 错误，`res.ok()` 即可统一判断，并暴露清晰的 `res.sqlstate()` 与 `res.error_message()`。
- **响应缓冲区所有权转移 (Buffer Ownership Transfer)**：`PostgresResult` 通过 Move 语义接管 `PostgresResponse` 缓冲区，彻底消除跨协程挂起点的悬垂指针与 Use-After-Free 内存隐患。
- **原生参数化查询 (Parameterized Queries)**：内置完整的 PostgreSQL 类型映射，支持 `$1, $2, ...` 占位符的 C++ 变参绑定，无需手动强转类型或拼接转义字符串。
- **对称类型解码器 (Typed Cell Decoders)**：提供 `as_bool()`, `as_int()`, `as_bigint()`, `as_double()`, `as_datetime()`, `as_uuid_string()`, `as_jsonb_string()`, `as_bytea()` 等完备提取接口。
- **稳健的会话与事务管理 (Connection & Transaction)**：`PostgresConnection` 封装底层 `WFPostgresConnection`，支持事务状态实时跟踪（`in_transaction()`, `is_transaction_failed()`）与受控优雅断开（`disconnect()`）。

---

## 📦 依赖组件 (Dependencies)

本项目使用 [xmake](https://xmake.io/) 作为默认构建系统，依赖以下核心组件：

- [workflow](https://github.com/sogou/workflow)：搜狗开源的 C++ 底层异步调度与网络通信框架。
- [coke](https://github.com/kedixa/coke)：针对 workflow 的 C++20 协程封装，提供对原生的 `co_await` 支持。
- [wf-postgres](https://github.com/tenire/wf-postgres)：基于 workflow 纯异步生态完整实现的 PostgreSQL 协议插件。
- **OpenSSL**：用于保障底层的 TLS 加密通信握手。

---

## 🚀 快速上手 (Quick Start)

### 1. 在项目中使用 / Integration

如果您的主项目使用 Xmake，建议通过 `package()` 直接引入本仓库：

```lua
-- xmake.lua
package("coke_postgres")
    add_deps("coke", "wf_postgres")
    add_urls("https://github.com/tenire/coke-postgres.git")
    on_install("linux", "macosx", function (package)
        import("package.tools.xmake").install(package)
    end)
package_end()

add_requires("coke_postgres")

target("my_app")
    set_kind("binary")
    set_languages("cxx20")
    add_files("src/*.cc")
    add_packages("coke_postgres")
```

### 2. 本地编译与测试 / Local Build & Test

```bash
# 默认构建静态库
xmake f -c -y
xmake

# 开启教程和测试的构建
xmake f --tutorial=y --tests=y
xmake

# 运行全套单元测试与离线校验
xmake run test_postgres

# 运行规范示例
xmake run tutorial_params
```

---

## 💡 代码示例 (Code Examples)

### 3. 基础查询示例 (Basic Query)

独立查询不需要显式会话，直接使用 `PostgresClient`：

```cpp
#include <iostream>
#include "ckpg/postgres.h"
#include "coke/wait.h"

coke::Task<int> hello_postgres(const ckpg::PostgresClientParams &params) {
    ckpg::PostgresClient cli(params);

    auto res = co_await cli.request("SELECT 'Hello, PostgreSQL!' AS greeting, current_timestamp;");
    
    // 统一状态判定：网络失败或 SQL 执行异常均返回 false
    if (!res.ok()) {
        std::cerr << "Query failed: " << res.error_message()
                  << " [SQLSTATE: " << res.sqlstate() << "]\n";
        co_return 1;
    }

    // 直接使用 PostgresResult 构造结果集视图（零拷贝访问）
    ckpg::PostgresResultSetView view(res);
    std::vector<ckpg::PostgresCellView> cells;

    while (view.next_row(cells)) {
        std::cout << cells[0].as_string() << "\t"
                  << cells[1].as_datetime_string() << "\n";
    }

    co_return 0;
}
```

### 4. 参数化查询 (Parameterized Query)

支持直接在 `request` 传入变参，类型安全，自动转换底层 PG 参数格式（无需手动书写 `$1::int`）：

```cpp
coke::Task<int> query_users(ckpg::PostgresClient &cli) {
    int user_id = 1001;
    std::string role = "admin";
    bool active = true;

    // 自动将参数类型和值绑定至 $1, $2, $3
    auto res = co_await cli.request(
        "SELECT id, username, is_active, score FROM users WHERE id = $1 AND role = $2 AND is_active = $3;",
        user_id, role, active
    );

    if (!res.ok()) {
        std::cerr << "Query error: " << res.error_message() << "\n";
        co_return 1;
    }

    ckpg::PostgresResultSetView view(res);
    std::vector<ckpg::PostgresCellView> cells;

    while (view.next_row(cells)) {
        // 使用强类型提取器
        int64_t id = cells[0].as_bigint();
        std::string username = cells[1].as_string();
        bool is_active = cells[2].as_bool();
        double score = cells[3].as_double();

        std::cout << "User: " << id << ", " << username
                  << ", active=" << (is_active ? "true" : "false")
                  << ", score=" << score << "\n";
    }

    co_return 0;
}
```

### 5. 事务级会话 (Transaction Session)

当需要执行 `BEGIN` / `COMMIT` / `ROLLBACK`、创建临时表或依赖连接级状态时，使用 `PostgresConnection`：

```cpp
coke::Task<int> transfer_funds(const ckpg::PostgresClientParams &params, int from_id, int to_id, double amount) {
    // 创建专用连接，内部持有独占的会话上下文
    ckpg::PostgresConnection conn(params);

    // 开启事务
    auto begin_res = co_await conn.request("BEGIN;");
    if (!begin_res.ok()) co_return 1;

    // 支持实时事务状态查询
    assert(conn.in_transaction());

    // 执行扣款与加款
    auto res1 = co_await conn.request("UPDATE accounts SET balance = balance - $1 WHERE id = $2;", amount, from_id);
    auto res2 = co_await conn.request("UPDATE accounts SET balance = balance + $1 WHERE id = $2;", amount, to_id);

    if (!res1.ok() || !res2.ok()) {
        // 事务中若发生 SQL 错误，conn.is_transaction_failed() 会自动变为 true
        std::cerr << "Transaction failed, rolling back...\n";
        co_await conn.request("ROLLBACK;");
        co_return 1;
    }

    co_await conn.request("COMMIT;");

    // 安全断开连接并回收底层资源
    co_await conn.disconnect();
    co_return 0;
}
```

---

## ⚠️ 生命周期与安全性说明 (Lifetime & Safety)

1. **响应所有权转移**：`PostgresResult` 内部持有响应数据的完整所有权，即使底层网络任务已被释放，结果在跨协程挂起或传递到外部函数后仍然安全有效。
2. **连接析构约束**：从同一 `PostgresConnection` 创建的所有 Awaiter 必须在连接析构前完成。连接析构时会释放其对应的逻辑连接标识。

---

## 📜 许可证 (License)

本项目采用与 Coke / Workflow 一致的开源协议发行。
