#include <iostream>
#include <string>

#include "ckpg/postgres_client.h"
#include "ckpg/postgres_utils.h"
#include "coke/wait.h"
#include "coke/global.h"

coke::Task<> run(const ckpg::PostgresClientParams &params)
{
    ckpg::PostgresClient cli(params);

    std::string sql;
    while (std::getline(std::cin, sql)) {
        if (sql == "quit" || sql == "\\q")
            break;

        auto res = co_await cli.request(sql);
        if (res.state != coke::STATE_SUCCESS) {
            std::cerr << "Request error: state=" << res.state << " error=" << res.error << "\n";
            continue;
        }

        ckpg::PostgresResultSetView view(res.resp);
        if (view.is_error()) {
            std::cerr << "Response error\n";
            continue;
        }

        std::vector<ckpg::PostgresCellView> cells;
        int row_count = 0;
        while (view.next_row(cells)) {
            for (size_t i = 0; i < cells.size(); ++i) {
                if (cells[i].is_null()) {
                    std::cout << "NULL";
                } else {
                    std::cout << cells[i].as_string();
                }
                if (i + 1 < cells.size()) std::cout << "\t|\t";
            }
            std::cout << "\n";
            row_count++;
        }
        std::cout << "(" << row_count << " rows)\n";
    }
}

int main(int argc, char **argv)
{
    ckpg::PostgresClientParams params;
    
    // minimal parse: host port dbname user pass
    if (argc >= 2) params.host = argv[1];
    if (argc >= 3) params.port = std::stoi(argv[2]);
    if (argc >= 4) params.dbname = argv[3];
    if (argc >= 5) params.username = argv[4];
    if (argc >= 6) params.password = argv[5];

    coke::sync_wait(run(params));
    return 0;
}
