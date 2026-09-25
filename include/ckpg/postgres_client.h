#ifndef CKPG_POSTGRES_CLIENT_H
#define CKPG_POSTGRES_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>
#include "coke/basic_awaiter.h"
#include "coke/global.h"
#include "workflow/URIParser.h"
#include "WFPostgresClient.h"

namespace ckpg {

using PostgresRequest = wfpg::protocol::PostgresRequest;
using PostgresResponse = wfpg::protocol::PostgresResponse;
using PostgresParameter = wfpg::protocol::PostgresParameter;
using PostgresStatus = wfpg::PostgresStatus;
using PostgresValue = wfpg::PostgresValue;
using PostgresResultCursor = wfpg::protocol::PostgresResultCursor;

struct PostgresResult {
    int state{coke::STATE_SYS_ERROR};
    int error{0};
    wfpg::WFPostgresTask *task{nullptr};
    wfpg::PostgresStatus status{wfpg::PostgresStatus::from_transport_error(coke::STATE_SYS_ERROR, 0)};
    wfpg::protocol::PostgresResponse resp_storage;
    wfpg::protocol::PostgresResponse *resp{&resp_storage};

    PostgresResult() = default;

    PostgresResult(int state, int error, wfpg::WFPostgresTask *task, wfpg::protocol::PostgresResponse *r)
        : state(state), error(error), task(task)
    {
        if (task) {
            status = wfpg::PostgresStatus::from_task(task);
        } else {
            status = wfpg::PostgresStatus::from_transport_error(state, error);
        }

        if (r) {
            resp_storage = std::move(*r);
        }
        resp = &resp_storage;
    }

    PostgresResult(PostgresResult&& other) noexcept
        : state(other.state),
          error(other.error),
          task(other.task),
          status(std::move(other.status)),
          resp_storage(std::move(other.resp_storage)),
          resp(&resp_storage)
    {
    }

    PostgresResult& operator=(PostgresResult&& other) noexcept {
        if (this != &other) {
            state = other.state;
            error = other.error;
            task = other.task;
            status = std::move(other.status);
            resp_storage = std::move(other.resp_storage);
            resp = &resp_storage;
        }
        return *this;
    }

    PostgresResult(const PostgresResult&) = delete;
    PostgresResult& operator=(const PostgresResult&) = delete;

    bool ok() const noexcept { return status.ok(); }
    explicit operator bool() const noexcept { return ok(); }
    bool is_error() const noexcept { return !ok(); }
    const std::string& sqlstate() const noexcept { return status.sqlstate(); }
    const std::string& error_message() const noexcept { return status.message(); }
    const wfpg::protocol::PostgresError& server_error() const noexcept { return status.server_error(); }

    wfpg::protocol::PostgresResponse& get_resp() noexcept { return resp_storage; }
    const wfpg::protocol::PostgresResponse& get_resp() const noexcept { return resp_storage; }

    wfpg::protocol::PostgresResultCursor get_cursor() {
        return wfpg::protocol::PostgresResultCursor(&resp_storage);
    }
};

class PostgresAwaiter : public coke::BasicAwaiter<PostgresResult> {
public:
    explicit PostgresAwaiter(wfpg::WFPostgresTask *task,
                            std::function<void(wfpg::WFPostgresTask*)> hook = nullptr) {
        if (!task) {
            PostgresResult res{coke::STATE_SYS_ERROR, 0, nullptr, nullptr};
            this->emplace_result(std::move(res));
            return;
        }

        task->set_callback([info = this->get_info(), hook = std::move(hook)](wfpg::WFPostgresTask *t) {
            if (hook) {
                hook(t);
            }
            auto *awaiter = info->get_awaiter<PostgresAwaiter>();
            if (awaiter) {
                PostgresResult res{t->get_state(), t->get_error(), t, t->get_resp()};
                awaiter->emplace_result(std::move(res));
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

    virtual AwaiterType request(const std::string &query);
    virtual AwaiterType request(const std::string &query,
                                const std::vector<PostgresParameter> &params);

    template <typename T, typename... Args,
              typename = typename std::enable_if<!std::is_convertible<typename std::decay<T>::type, std::vector<PostgresParameter>>::value>::type>
    AwaiterType request(const std::string &query, const T &first, const Args &...rest) {
        return request(query, wfpg::bind_params(first, rest...));
    }

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

    AwaiterType request(const std::string &query) override;
    AwaiterType request(const std::string &query,
                        const std::vector<PostgresParameter> &params) override;

    template <typename T, typename... Args,
              typename = typename std::enable_if<!std::is_convertible<typename std::decay<T>::type, std::vector<PostgresParameter>>::value>::type>
    AwaiterType request(const std::string &query, const T &first, const Args &...rest) {
        return request(query, wfpg::bind_params(first, rest...));
    }

    AwaiterType disconnect();
    AwaiterType cancel();

    char get_last_transaction_state() const;
    bool in_transaction() const;
    bool is_transaction_failed() const;
    int32_t get_backend_pid() const;

private:
    static std::size_t acquire_conn_id();
    static void release_conn_id(std::size_t conn_id);

    std::unique_ptr<wfpg::WFPostgresConnection> conn_;
};

} // namespace ckpg

#endif
