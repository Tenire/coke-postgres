#include <iostream>
#include <string>
#include <vector>

#include "ckpg/postgres.h"
#include "coke/wait.h"

// Demonstrates canonical parameterized queries with automatic type binding
coke::Task<int> demo_parameterized_query(const ckpg::PostgresClientParams &params) {
    ckpg::PostgresClient cli(params);

    std::cout << "--- Demo 1: Parameterized Query ---\n";
    // Native variadic arguments: $1 (int), $2 (text), $3 (bool), $4 (float8)
    // Pure $1, $2, $3, $4 without any explicit :: casts!
    auto res = co_await cli.request(
        "SELECT $1 AS id, $2 AS name, $3 AS active, $4 AS score;",
        1001, "Alice", true, 98.5
    );
    // Canonical error check: unified transport & SQL error status
    if (!res.ok()) {
        std::cerr << "Query failed: " << res.error_message()
                  << " [SQLSTATE: " << res.sqlstate() << "]\n";
        co_return 1;
    }

    ckpg::PostgresResultSetView view(res);
    std::vector<ckpg::PostgresCellView> cells;

    while (view.next_row(cells)) {
        // High-level typed decoders directly from PostgresCellView
        int id = cells[0].as_int();
        std::string name = cells[1].as_string();
        bool active = cells[2].as_bool();
        double score = cells[3].as_double();

        std::cout << "Row: id=" << id
                  << ", name=" << name
                  << ", active=" << (active ? "true" : "false")
                  << ", score=" << score << "\n";
    }

    co_return 0;
}

// Demonstrates safe transaction management with PostgresConnection
coke::Task<int> demo_transaction(const ckpg::PostgresClientParams &params) {
    std::cout << "--- Demo 2: Transaction Session ---\n";
    ckpg::PostgresConnection conn(params);

    // 1. Begin transaction
    auto begin_res = co_await conn.request("BEGIN;");
    if (!begin_res.ok()) {
        std::cerr << "BEGIN failed: " << begin_res.error_message() << "\n";
        co_return 1;
    }
    std::cout << "In transaction? " << (conn.in_transaction() ? "yes" : "no") << "\n";

    // 2. Execute statements inside transaction
    co_await conn.request("CREATE TEMP TABLE ckpg_demo_users (id int, name text);");
    co_await conn.request("INSERT INTO ckpg_demo_users VALUES ($1, $2);", 1, "Bob");
    co_await conn.request("INSERT INTO ckpg_demo_users VALUES ($1, $2);", 2, "Charlie");

    // 3. Query back
    auto select_res = co_await conn.request("SELECT id, name FROM ckpg_demo_users ORDER BY id;");
    if (select_res.ok()) {
        ckpg::PostgresResultSetView view(select_res);
        std::vector<ckpg::PostgresCellView> cells;
        while (view.next_row(cells)) {
            std::cout << "User #" << cells[0].as_int() << ": " << cells[1].as_string() << "\n";
        }
    }

    // 4. Commit or Rollback
    auto commit_res = co_await conn.request("COMMIT;");
    if (!commit_res.ok()) {
        std::cerr << "COMMIT failed: " << commit_res.error_message() << "\n";
        co_return 1;
    }
    std::cout << "After commit, in transaction? " << (conn.in_transaction() ? "yes" : "no") << "\n";

    // 5. Explicit safe disconnect
    auto dis = co_await conn.disconnect();
    if (!dis.ok()) {
        std::cerr << "disconnect failed: " << dis.error_message() << "\n";
        co_return 1;
    }

    co_return 0;
}

int main(int argc, char **argv) {
    ckpg::PostgresClientParams params;
    params.host = "127.0.0.1";
    params.port = 5432;
    params.dbname = "mapauth";
    params.username = "mapauth";
    params.password = "your_password";

    if (argc >= 2) params.host = argv[1];
    if (argc >= 3) params.port = std::stoi(argv[2]);
    if (argc >= 4) params.dbname = argv[3];
    if (argc >= 5) params.username = argv[4];
    if (argc >= 6) params.password = argv[5];

    int ret = coke::sync_wait(demo_parameterized_query(params));
    if (ret != 0) return ret;

    return coke::sync_wait(demo_transaction(params));
}
