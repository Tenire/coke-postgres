# coke-postgres

English | [简体中文](README_CN.md)

**coke-postgres** is a C++20 coroutine wrapper for [wf-postgres](https://github.com/tenire/wf-postgres), built on top of [coke](https://github.com/kedixa/coke) and [Sogou Workflow](https://github.com/sogou/workflow).

It delivers a modern, high-performance, and memory-safe interface for asynchronous PostgreSQL access with `co_await`, featuring unified status handling, buffer ownership transfer, native parameterized queries, and robust connection/transaction lifecycle management.

---

## ✨ Features

- **Native C++20 Coroutines**: Full `co_await` syntax eliminating callback hell while preserving Workflow's event-driven concurrency.
- **Unified Status**: Unifies transport failures and PostgreSQL protocol server errors into a single check via `res.ok()`, with `res.sqlstate()`, `res.error_message()`, and `res.affected_rows()`.
- **Buffer Ownership Transfer**: `PostgresResult` takes full ownership of the response buffer via move semantics in `PostgresAwaiter`, preventing dangling pointers and Use-After-Free (UAF) hazards across coroutine suspension points.
- **Native Parameterized Queries & Chrono**: Built-in support for PostgreSQL type matrix via C++ variadic arguments (`$1, $2, ...`) without manual string escaping or `::type` casts; native microsecond-precision `std::chrono::system_clock::time_point` binding and extraction (`as_time_point()`, `as_sys_time()`).
- **Symmetric Typed Decoders**: Direct extraction through `PostgresCellView` (`as_bool()`, `as_int()`, `as_bigint()`, `as_double()`, `as_time_point()`, `as_sys_time()`, `as_datetime()`, `as_uuid_string()`, `as_jsonb_string()`, `as_bytea()`).
- **Connection & Transaction Tracking**: `PostgresConnection` encapsulates `WFPostgresConnection`, providing real-time transaction state tracking (`in_transaction()`, `is_transaction_failed()`) and controlled graceful termination (`disconnect()`).
---

## 📦 Dependencies

Built with [xmake](https://xmake.io/) and relies on:

- [workflow](https://github.com/sogou/workflow): High-performance asynchronous parallel framework.
- [coke](https://github.com/kedixa/coke): C++20 coroutine wrapper for Workflow tasks.
- [wf-postgres](https://github.com/tenire/wf-postgres): Asynchronous PostgreSQL wire protocol client.
- **OpenSSL**: Secure STARTTLS and authentication cryptography.

---

## 🚀 Quick Start

### 1. Project Integration

Add `coke-postgres` to your `xmake.lua`:

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

### 2. Build and Test Locally

```bash
# Build static library
xmake f -c -y
xmake

# Build tutorials and test suite
xmake f --tutorial=y --tests=y
xmake

# Run unit and integration tests
xmake run test_postgres

# Run parameterized query and transaction tutorial
xmake run tutorial_params
```

---

## 💡 Code Examples

### 3. Basic Query

Stateless queries can be dispatched directly with `PostgresClient`:

```cpp
#include <iostream>
#include "ckpg/postgres.h"
#include "coke/wait.h"

coke::Task<int> hello_postgres(const std::string &database_url) {
    // Construct directly with a connection URL
    ckpg::PostgresClient cli(database_url);

    auto res = co_await cli.request("SELECT 'Hello, PostgreSQL!' AS greeting, current_timestamp;");
    
    // Unified error check: false on transport failure or SQL execution error
    if (!res.ok()) {
        std::cerr << "Query failed: " << res.error_message()
                  << " [SQLSTATE: " << res.sqlstate() << "]\n";
        co_return 1;
    }

    // Zero-copy result view backed by owned response buffer
    ckpg::PostgresResultSetView view(res);
    std::vector<ckpg::PostgresCellView> cells;

    while (view.next_row(cells)) {
        std::cout << cells[0].as_string() << "\t"
                  << cells[1].as_datetime_string() << "\n";
    }

    co_return 0;
}
```

### 4. Parameterized Query

Pass query parameters naturally as variadic arguments; types are automatically mapped to PostgreSQL OIDs without manual `::type` casts:

```cpp
coke::Task<int> query_users(ckpg::PostgresClient &cli) {
    int user_id = 1001;
    std::string role = "admin";
    bool active = true;

    // Parameters bind directly to $1, $2, $3 with type safety
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
        // High-level typed decoders
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

### 5. Transaction Session

For `BEGIN`, `COMMIT`, `ROLLBACK`, temporary tables, or connection-scoped operations, use `PostgresConnection`:

```cpp
coke::Task<int> transfer_funds(const ckpg::PostgresClientParams &params, int from_id, int to_id, double amount) {
    // Dedicated connection owning a logical session context
    ckpg::PostgresConnection conn(params);

    // 1. Begin transaction
    auto begin_res = co_await conn.request("BEGIN;");
    if (!begin_res.ok()) co_return 1;

    // Inspect real-time transaction state
    assert(conn.in_transaction());

    // 2. Execute statements
    auto res1 = co_await conn.request("UPDATE accounts SET balance = balance - $1 WHERE id = $2;", amount, from_id);
    auto res2 = co_await conn.request("UPDATE accounts SET balance = balance + $1 WHERE id = $2;", amount, to_id);

    if (!res1.ok() || !res2.ok()) {
        // If SQL error occurs, conn.is_transaction_failed() automatically becomes true
        std::cerr << "Transaction failed, rolling back...\n";
        co_await conn.request("ROLLBACK;");
        co_return 1;
    }

    // 3. Commit
    co_await conn.request("COMMIT;");

    // 4. Gracefully disconnect and release resources
    co_await conn.disconnect();
    co_return 0;
}
```

---

## ⚠️ Lifetime & Safety Rules

1. **Owned Buffers**: `PostgresResult` holds full ownership of the response buffer. Results remain valid across coroutine suspension points or when returned out of scope.
2. **Connection Invariant**: All awaitables created from a `PostgresConnection` must complete before the connection object is destroyed.

---

## 📜 License

Licensed under the same Apache 2.0 license as Coke and Workflow.
