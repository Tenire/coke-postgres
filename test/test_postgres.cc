#include <iostream>
#include <stdlib.h>
#include "ckpg/postgres.h"
#include "coke/wait.h"

coke::Task<int> test_query(ckpg::PostgresClientParams params) {
    ckpg::PostgresClient cli(params);
    
    auto res = co_await cli.request("SELECT 1;");
    if (res.state != coke::STATE_SUCCESS) {
        std::cerr << "Request failed state=" << res.state << " error=" << res.error << "\n";
        co_return 1;
    }

    ckpg::PostgresResultSetView view(res.resp);
    if (view.is_error()) {
        std::cerr << "Query returned error: " << (res.resp ? res.resp->get_error_msg() : "null response") << "\n";
        co_return 1;
    }

    std::vector<ckpg::PostgresCellView> cells;
    
    int row_count = 0;
    while (view.next_row(cells)) {
        if (cells.size() != 1 || cells[0].as_string() != "1") {
            std::cerr << "Unexpected result content\n";
            co_return 1;
        }
        row_count++;
    }

    if (row_count != 1) {
        std::cerr << "Unexpected row count\n";
        co_return 1;
    }

    co_return 0;
}

coke::Task<int> test_connection_session(ckpg::PostgresClientParams params) {
    ckpg::PostgresConnection conn(params);

    const char *sqls[] = {
        "BEGIN;",
        "CREATE TEMP TABLE ckpg_tmp_test(v int);",
        "INSERT INTO ckpg_tmp_test VALUES (42);",
        "SELECT v FROM ckpg_tmp_test;",
        "ROLLBACK;"
    };

    for (size_t i = 0; i < 5; ++i) {
        auto res = co_await conn.request(sqls[i]);
        if (res.state != coke::STATE_SUCCESS || res.resp == nullptr || res.resp->is_error()) {
            std::cerr << "test_connection_session failed on: " << sqls[i] << "\n";
            co_return 1;
        }

        if (i == 3) {
            ckpg::PostgresResultSetView view(res.resp);
            std::vector<ckpg::PostgresCellView> cells;
            if (!view.next_row(cells) || cells.size() != 1 || cells[0].as_string() != "42") {
                std::cerr << "test_connection_session unexpected SELECT result\n";
                co_return 1;
            }
        }
    }

    auto dis = co_await conn.disconnect();
    if (dis.state != coke::STATE_SUCCESS) {
        std::cerr << "test_connection_session disconnect failed\n";
        co_return 1;
    }

    co_return 0;
}

int test_null_view()
{
    ckpg::PostgresResultSetView view(nullptr);
    std::vector<ckpg::PostgresCellView> cells;

    if (!view.is_error())
        return 1;
    if (view.is_ok())
        return 1;
    if (view.get_field_count() != 0)
        return 1;
    if (view.next_row(cells))
        return 1;
    if (!cells.empty())
        return 1;

    return 0;
}

int main(int argc, char **argv) {
    if (test_null_view() != 0) {
        std::cerr << "test_null_view failed\n";
        return 1;
    }

    const char* host = getenv("COKE_POSTGRES_HOST");
    if (!host) {
        std::cout << "Skipped database test because COKE_POSTGRES_HOST is not set.\n";
        return 0;
    }

    ckpg::PostgresClientParams params;
    params.host = host;
    
    if (const char* port = getenv("COKE_POSTGRES_PORT")) {
        params.port = std::stoi(port);
    }
    if (const char* user = getenv("COKE_POSTGRES_USER")) {
        params.username = user;
    }
    if (const char* pass = getenv("COKE_POSTGRES_PASSWORD")) {
        params.password = pass;
    }
    if (const char* db = getenv("COKE_POSTGRES_DB")) {
        params.dbname = db;
    }

    int ret = coke::sync_wait(test_query(params));
    if (ret != 0)
        return ret;

    int conn_ret = coke::sync_wait(test_connection_session(params));
    if (conn_ret != 0)
        return conn_ret;

    return 0;
}
