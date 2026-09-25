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
coke::Task<int> test_chrono_and_outcome(ckpg::PostgresClientParams params) {
    ckpg::PostgresConnection conn(params);

    auto res_create = co_await conn.request(
        "CREATE TEMP TABLE ckpg_chrono_test(id int, ts timestamptz, note text);"
    );
    if (!res_create.ok()) {
        std::cerr << "Failed to create temp table: " << res_create.error_message() << "\n";
        co_return 1;
    }

    // Fixed microsecond instant: 2026-09-25 15:30:45.123456 UTC
    using namespace std::chrono;
    auto test_tp = sys_time<microseconds>(seconds(1790350245) + microseconds(123456));

    // Test parameterized insert with time_point
    auto res_insert = co_await conn.request(
        "INSERT INTO ckpg_chrono_test (id, ts, note) VALUES ($1, $2, $3);",
        1, test_tp, "instant_test"
    );

    if (!res_insert.ok()) {
        std::cerr << "Insert failed: " << res_insert.error_message() << "\n";
        co_return 1;
    }

    if (res_insert.affected_rows() != 1) {
        std::cerr << "Expected affected_rows=1, got " << res_insert.affected_rows() << "\n";
        co_return 1;
    }

    if (res_insert.command_tag() != "INSERT 0 1") {
        std::cerr << "Unexpected command_tag: " << res_insert.command_tag() << "\n";
        co_return 1;
    }

    // Query back
    auto res_select = co_await conn.request(
        "SELECT id, ts, note FROM ckpg_chrono_test WHERE id = $1;", 1
    );
    if (!res_select.ok()) {
        std::cerr << "Select failed: " << res_select.error_message() << "\n";
        co_return 1;
    }

    ckpg::PostgresResultSetView view(res_select);
    std::vector<ckpg::PostgresCellView> cells;

    if (!view.next_row(cells) || cells.size() != 3) {
        std::cerr << "Failed to fetch row from ckpg_chrono_test\n";
        co_return 1;
    }

    // Verify strong typed chrono decoder
    auto read_tp = cells[1].as_time_point();
    auto read_us = time_point_cast<microseconds>(read_tp);
    if (read_us != test_tp) {
        std::cerr << "Chrono roundtrip mismatch! Expected "
                  << test_tp.time_since_epoch().count()
                  << " us, got " << read_us.time_since_epoch().count() << " us\n";
        co_return 1;
    }

    auto read_sys = cells[1].as_sys_time<microseconds>();
    if (read_sys != test_tp) {
        std::cerr << "as_sys_time mismatch!\n";
        co_return 1;
    }

    if (!cells[1].as_optional_time_point().has_value()) {
        std::cerr << "as_optional_time_point empty!\n";
        co_return 1;
    }

    if (!cells[2].as_optional_string().has_value() ||
        cells[2].as_optional_string().value() != "instant_test") {
        std::cerr << "as_optional_string failed\n";
        co_return 1;
    }

    // Test UPDATE affected rows
    auto res_update = co_await conn.request(
        "UPDATE ckpg_chrono_test SET note = $1 WHERE id = $2;", "updated", 1
    );
    if (!res_update.ok() || res_update.affected_rows() != 1) {
        std::cerr << "Update failed or affected_rows mismatch\n";
        co_return 1;
    }

    co_await conn.disconnect();
    co_return 0;
}

coke::Task<int> test_client_url_construction(const std::string &url) {
    ckpg::PostgresClient cli(url);
    auto res = co_await cli.request("SELECT 'url_client_ok' AS msg;");
    if (!res.ok()) {
        std::cerr << "URL client query failed: " << res.error_message() << "\n";
        co_return 1;
    }

    ckpg::PostgresConnection conn(url);
    auto conn_res = co_await conn.request("SELECT 'url_conn_ok' AS msg;");
    if (!conn_res.ok()) {
        std::cerr << "URL connection query failed: " << conn_res.error_message() << "\n";
        co_return 1;
    }
    co_await conn.disconnect();
    co_return 0;
}

coke::Task<int> test_parameterized_query(ckpg::PostgresClientParams params) {
    ckpg::PostgresClient cli(params);

    auto res = co_await cli.request("SELECT $1, $2, $3, $4;",
                                   42, "coke-postgres", true, 3.14159);
    if (!res.ok() || res.state != coke::STATE_SUCCESS) {
        std::cerr << "test_parameterized_query failed: " << res.error_message() << "\n";
        co_return 1;
    }

    ckpg::PostgresResultSetView view(res);
    if (!view.is_ok() || view.get_field_count() != 4) {
        std::cerr << "test_parameterized_query invalid view or field count\n";
        co_return 1;
    }

    std::vector<ckpg::PostgresCellView> cells;
    if (!view.next_row(cells) || cells.size() != 4) {
        std::cerr << "test_parameterized_query row fetch failed\n";
        co_return 1;
    }

    if (cells[0].as_int() != 42) {
        std::cerr << "test_parameterized_query as_int mismatch: " << cells[0].as_int() << "\n";
        co_return 1;
    }
    if (cells[1].as_string() != "coke-postgres") {
        std::cerr << "test_parameterized_query as_string mismatch: " << cells[1].as_string() << "\n";
        co_return 1;
    }
    if (!cells[2].as_bool()) {
        std::cerr << "test_parameterized_query as_bool mismatch\n";
        co_return 1;
    }
    if (cells[3].as_double() < 3.14 || cells[3].as_double() > 3.15) {
        std::cerr << "test_parameterized_query as_double mismatch: " << cells[3].as_double() << "\n";
        co_return 1;
    }

    co_return 0;
}

coke::Task<int> test_transaction_error_and_state(ckpg::PostgresClientParams params) {
    ckpg::PostgresConnection conn(params);

    if (conn.in_transaction() || conn.is_transaction_failed()) {
        std::cerr << "initial transaction state invalid\n";
        co_return 1;
    }

    auto res1 = co_await conn.request("BEGIN;");
    if (!res1.ok()) {
        std::cerr << "BEGIN failed\n";
        co_return 1;
    }
    if (!conn.in_transaction() || conn.is_transaction_failed()) {
        std::cerr << "state after BEGIN invalid, in_tx=" << conn.in_transaction() << "\n";
        co_return 1;
    }

    // Intentionally trigger SQL error
    auto res_err = co_await conn.request("SELECT * FROM non_existent_table_ckpg_12345;");
    if (res_err.ok() || !res_err.is_error()) {
        std::cerr << "expected error but got success\n";
        co_return 1;
    }
    if (res_err.sqlstate() != "42P01") {
        std::cerr << "unexpected sqlstate: " << res_err.sqlstate() << "\n";
        co_return 1;
    }
    if (!conn.is_transaction_failed()) {
        std::cerr << "expected is_transaction_failed() to be true\n";
        co_return 1;
    }

    auto res_rb = co_await conn.request("ROLLBACK;");
    if (!res_rb.ok()) {
        std::cerr << "ROLLBACK failed\n";
        co_return 1;
    }
    if (conn.in_transaction() || conn.is_transaction_failed()) {
        std::cerr << "state after ROLLBACK invalid\n";
        co_return 1;
    }

    auto dis = co_await conn.disconnect();
    if (!dis.ok()) {
        std::cerr << "disconnect failed\n";
        co_return 1;
    }

    co_return 0;
}

int test_offline_status_and_ownership()
{
    // 1. Default status & error status
    ckpg::PostgresResult default_res;
    if (default_res.ok()) return 1;
    if (!default_res.is_error()) return 1;

    // 2. Server error status
    wfpg::protocol::PostgresError err;
    err.severity = "ERROR";
    err.sql_state = "23505";
    err.message = "unique violation";
    wfpg::PostgresStatus status = wfpg::PostgresStatus::from_server_error(err);

    if (status.ok()) return 2;
    if (status.sqlstate() != "23505") return 3;
    if (status.message() != "unique violation") return 4;

    // 3. Move semantics & safe ownership transfer
    ckpg::PostgresResult res;
    res.status = status;
    res.state = coke::STATE_SUCCESS;
    
    ckpg::PostgresResult moved_res = std::move(res);
    if (moved_res.ok()) return 5;
    if (moved_res.sqlstate() != "23505") return 6;
    if (moved_res.resp != &moved_res.resp_storage) return 7;

    return 0;
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
int test_url_parsing()
{
    std::string url = "postgresqls://alice:secret%3Apass@db.example.com:5433/production_db?application_name=my_service&sslmode=require";
    auto params = ckpg::PostgresClientParams::from_url(url);

    if (!params.use_ssl) return 1;
    if (params.host != "db.example.com") return 2;
    if (params.port != 5433) return 3;
    if (params.username != "alice") return 4;
    if (params.password != "secret:pass") return 5;
    if (params.dbname != "production_db") return 6;
    if (params.application_name != "my_service") return 7;

    // Test default port and postgres:// scheme
    std::string url2 = "postgres://bob@127.0.0.1/testdb";
    auto p2 = ckpg::PostgresClientParams::from_url(url2);
    if (p2.use_ssl) return 8;
    if (p2.port != 5432) return 9;
    if (p2.username != "bob") return 10;
    if (!p2.password.empty()) return 11;
    if (p2.dbname != "testdb") return 12;

    return 0;
}


int main(int argc, char **argv) {
    if (test_null_view() != 0) {
        std::cerr << "test_null_view failed\n";
        return 1;
    }
    std::cout << "[Test 1/7] test_null_view passed\n";
    if (test_url_parsing() != 0) {
        std::cerr << "test_url_parsing failed\n";
        return 1;
    }
    std::cout << "[Test 2/7] test_url_parsing passed\n";

    int rc = test_offline_status_and_ownership();
    if (rc != 0) {
        std::cerr << "test_offline_status_and_ownership failed rc=" << rc << "\n";
        return 1;
    }
    std::cout << "[Test 3/7] test_offline_status_and_ownership passed\n";

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
    std::cout << "[Test 4/7] test_query passed\n";

    int conn_ret = coke::sync_wait(test_connection_session(params));
    if (conn_ret != 0)
        return conn_ret;
    std::cout << "[Test 5/7] test_connection_session passed\n";

    int param_ret = coke::sync_wait(test_parameterized_query(params));
    if (param_ret != 0)
        return param_ret;
    std::cout << "[Test 6/7] test_parameterized_query passed\n";

    int chrono_ret = coke::sync_wait(test_chrono_and_outcome(params));
    if (chrono_ret != 0)
        return chrono_ret;
    std::cout << "[Test 7/7] test_chrono_and_outcome passed\n";

    // Construct URL and test URL-based client & connection
    std::string conn_url = "postgresql://" + params.username + ":" + params.password +
                           "@" + params.host + ":" + std::to_string(params.port) +
                           "/" + params.dbname;
    int url_cli_ret = coke::sync_wait(test_client_url_construction(conn_url));
    if (url_cli_ret != 0)
        return url_cli_ret;
    std::cout << "[Extra] test_client_url_construction passed\n";

    int tx_err_ret = coke::sync_wait(test_transaction_error_and_state(params));
    if (tx_err_ret != 0)
        return tx_err_ret;
    std::cout << "[Extra] test_transaction_error_and_state passed\n";

    std::cout << "All unit and integration tests passed successfully!\n";
    return 0;
}
