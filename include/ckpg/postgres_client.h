#ifndef CKPG_POSTGRES_CLIENT_H
#define CKPG_POSTGRES_CLIENT_H

#include <string>
#include "coke/basic_awaiter.h"
#include "coke/global.h"
#include "workflow/URIParser.h"
#include "PostgresTask.h"


namespace ckpg {

using PostgresRequest = wfpg::protocol::PostgresRequest;
using PostgresResponse = wfpg::protocol::PostgresResponse;

struct PostgresResult {
    int state;
    int error;
    wfpg::WFPostgresTask *task;
    wfpg::protocol::PostgresResponse *resp;

    // The task and resp pointers are valid only until the task is destroyed.
    // Do not save these pointers across co_await boundaries.
};

class PostgresAwaiter : public coke::BasicAwaiter<PostgresResult> {
public:
    explicit PostgresAwaiter(wfpg::WFPostgresTask *task) {
        if (!task) {
            PostgresResult res{coke::STATE_SYS_ERROR, 0, nullptr, nullptr};
            this->emplace_result(res);
            // Do not call done() here, await_ready handles null tasks
            return;
        }

        task->set_callback([info = this->get_info()](wfpg::WFPostgresTask *t) {
            auto *awaiter = info->get_awaiter<PostgresAwaiter>();
            if (awaiter) {
                PostgresResult res{t->get_state(), t->get_error(), t, t->get_resp()};
                awaiter->emplace_result(res);
                awaiter->done();
            }
        });
        set_task(task);
    }
};

struct PostgresClientParams {
    int retry_max = 0;
    int send_timeout = -1;
    int receive_timeout = -1;
    int keep_alive_timeout = 60 * 1000;

    bool use_ssl = false;
    int port = 5432;
    std::string host;
    std::string username;
    std::string password;
    std::string dbname;
    std::string application_name;
};

class PostgresClient {
public:
    using ReqType = PostgresRequest;
    using RespType = PostgresResponse;
    using AwaiterType = PostgresAwaiter;

    explicit PostgresClient(const PostgresClientParams &params);
    virtual ~PostgresClient() = default;

    PostgresClientParams get_params() const { return params; }

    AwaiterType request(const std::string &query);

protected:
    PostgresClient(const PostgresClientParams &params,
                   bool unique_conn,
                   std::size_t conn_id);

protected:
    bool unique_conn;
    bool valid_uri{false};
    std::size_t conn_id;
    PostgresClientParams params;

    std::string url;
    ParsedURI uri;
};

/**
 * Logical PostgreSQL connection.
 *
 * The object owns a logical transaction routing id. All awaiters created from
 * this connection must complete before the connection is destroyed.
 */
class PostgresConnection : public PostgresClient {
public:
    explicit PostgresConnection(const PostgresClientParams &params);
    ~PostgresConnection() override;

    PostgresConnection(const PostgresConnection&) = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;
    PostgresConnection(PostgresConnection&&) = delete;
    PostgresConnection& operator=(PostgresConnection&&) = delete;

    AwaiterType disconnect();
    
private:
    static std::size_t acquire_conn_id();
    static void release_conn_id(std::size_t conn_id);
};

} // namespace ckpg

#endif
