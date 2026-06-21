#include "ckpg/postgres_client.h"
#include "workflow/StringUtil.h"
#include "PostgresTask.h"
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
namespace ckpg {

namespace {

struct PostgresConnIdPool {
    static PostgresConnIdPool *get_instance()
    {
        static PostgresConnIdPool instance;
        return &instance;
    }

    std::size_t acquire()
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (free_ids.empty())
            return next_id++;

        std::size_t id = free_ids.top();
        free_ids.pop();
        return id;
    }

    void release(std::size_t id)
    {
        if (id == 0)
            return;

        std::lock_guard<std::mutex> lock(mtx);
        free_ids.push(id);
    }

private:
    std::mutex mtx;
    std::size_t next_id{1};
    std::priority_queue<
        std::size_t,
        std::vector<std::size_t>,
        std::greater<std::size_t>
    > free_ids;
};

} // namespace

PostgresClient::PostgresClient(const PostgresClientParams &p)
    : PostgresClient(p, false, 0)
{
}

PostgresClient::PostgresClient(const PostgresClientParams &p,
                               bool unique_conn,
                               std::size_t conn_id)
    : unique_conn(unique_conn), conn_id(conn_id), params(p)
{
    std::string user = StringUtil::url_encode_component(params.username);
    std::string pass = StringUtil::url_encode_component(params.password);
    std::string db = StringUtil::url_encode_component(params.dbname);

    url.assign(params.use_ssl ? "postgresqls://" : "postgresql://");

    if (!user.empty() || !pass.empty()) {
        url.append(user);
        if (!pass.empty()) {
            url.append(":").append(pass);
        }
        url.append("@");
    }

    std::string h = params.host;
    if (h.find(':') != std::string::npos && h.front() != '[') {
        url.append("[").append(h).append("]");
    } else {
        url.append(h);
    }

    url.append(":").append(std::to_string(params.port));
    url.append("/").append(db);

    bool has_query = (url.find('?') != std::string::npos);

    auto append_query = [&](const std::string &key, const std::string &value) {
        url.push_back(has_query ? '&' : '?');
        has_query = true;
        url.append(key)
           .append("=")
           .append(StringUtil::url_encode_component(value));
    };

    if (!params.application_name.empty())
        append_query("application_name", params.application_name);

    if (unique_conn) {
        params.retry_max = 0;
        append_query("transaction", "ckpg_postgres_transaction_id_" + std::to_string(conn_id));
    }

    // URIParser::parse returns 0 on success
    if (URIParser::parse(url, uri) == 0) {
        valid_uri = true;
    } else {
        valid_uri = false;
    }
}

PostgresClient::AwaiterType PostgresClient::request(const std::string &query)
{
    if (!valid_uri) {
        return AwaiterType(nullptr);
    }

    wfpg::WFPostgresTask *task = wfpg::WFPostgresTaskFactory::create_postgres_task(uri, params.retry_max, nullptr);

    if (task) {
        task->get_req()->set_query(query);
        task->set_send_timeout(params.send_timeout);
        task->set_receive_timeout(params.receive_timeout);
        task->set_keep_alive(params.keep_alive_timeout);
    }

    return AwaiterType(task);
}

PostgresConnection::PostgresConnection(const PostgresClientParams &p)
    : PostgresClient(p, true, acquire_conn_id())
{
}

PostgresConnection::~PostgresConnection()
{
    release_conn_id(conn_id);
}

PostgresClient::AwaiterType PostgresConnection::disconnect()
{
    if (!valid_uri) {
        return AwaiterType(nullptr);
    }
    auto *task = wfpg::WFPostgresTaskFactory::create_disconnect_task(
        uri, params.retry_max, nullptr);

    if (task) {
        task->set_send_timeout(params.send_timeout);
        task->set_receive_timeout(params.receive_timeout);
        task->set_keep_alive(0);
    }

    return AwaiterType(task);
}

std::size_t PostgresConnection::acquire_conn_id()
{
    return PostgresConnIdPool::get_instance()->acquire();
}

void PostgresConnection::release_conn_id(std::size_t conn_id)
{
    PostgresConnIdPool::get_instance()->release(conn_id);
}

} // namespace ckpg
