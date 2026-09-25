#include "ckpg/postgres_client.h"
#include "workflow/StringUtil.h"
#include "WFPostgresClient.h"
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
std::string url_decode_str(std::string str)
{
    StringUtil::url_decode(str);
    return str;
}

} // namespace
PostgresClientParams PostgresClientParams::from_url(std::string_view url_view)
{
    PostgresClientParams params;
    std::string url_str(url_view);
    ParsedURI uri;
    if (URIParser::parse(url_str, uri) != 0) {
        return params;
    }

    if (uri.scheme) {
        std::string_view scheme(uri.scheme);
        if (scheme == "postgresqls" || scheme == "postgresqless" || scheme == "postgres+ssl") {
            params.use_ssl = true;
        }
    }

    if (uri.host) {
        params.host = uri.host;
    }

    if (uri.port) {
        params.port = std::atoi(uri.port);
    } else {
        params.port = 5432;
    }

    if (uri.userinfo) {
        std::string_view uinfo(uri.userinfo);
        auto pos = uinfo.find(':');
        if (pos != std::string_view::npos) {
            params.username = url_decode_str(std::string(uinfo.substr(0, pos)));
            params.password = url_decode_str(std::string(uinfo.substr(pos + 1)));
        } else {
            params.username = url_decode_str(std::string(uinfo));
        }
    }

    if (uri.path) {
        std::string_view p(uri.path);
        if (!p.empty() && p.front() == '/') {
            p.remove_prefix(1);
        }
        params.dbname = url_decode_str(std::string(p));
    }

    if (uri.query) {
        std::string q(uri.query);
        auto pairs = StringUtil::split(q, '&');
        for (const auto &pair : pairs) {
            auto kv = StringUtil::split(pair, '=');
            if (kv.size() >= 2) {
                std::string k = kv[0];
                std::string v = url_decode_str(kv[1]);
                if (k == "application_name") {
                    params.application_name = v;
                } else if (k == "sslmode") {
                    if (v == "require" || v == "verify-ca" || v == "verify-full") {
                        params.use_ssl = true;
                    } else if (v == "disable") {
                        params.use_ssl = false;
                    }
                }
            }
        }
    }

    return params;
}

PostgresClient::PostgresClient(std::string_view url)
    : PostgresClient(PostgresClientParams::from_url(url))
{
}

PostgresClient::PostgresClient(std::string_view url, const PostgresClientParams &defaults)
    : PostgresClient([&]() {
        PostgresClientParams p = defaults;
        PostgresClientParams parsed = PostgresClientParams::from_url(url);
        p.use_ssl = parsed.use_ssl;
        p.port = parsed.port;
        p.host = std::move(parsed.host);
        p.username = std::move(parsed.username);
        p.password = std::move(parsed.password);
        p.dbname = std::move(parsed.dbname);
        if (!parsed.application_name.empty()) {
            p.application_name = std::move(parsed.application_name);
        }
        return p;
    }())
{
}

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
        // connection routing is managed by WFPostgresConnection
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

PostgresClient::AwaiterType PostgresClient::request(
    const std::string &query,
    const std::vector<PostgresParameter> &params_vec)
{
    if (!valid_uri) {
        return AwaiterType(nullptr);
    }

    wfpg::WFPostgresTask *task = wfpg::WFPostgresTaskFactory::create_postgres_task(uri, params.retry_max, nullptr);

    if (task) {
        task->get_req()->set_query(query, params_vec);
        task->set_send_timeout(params.send_timeout);
        task->set_receive_timeout(params.receive_timeout);
        task->set_keep_alive(params.keep_alive_timeout);
    }

    return AwaiterType(task);
}

PostgresConnection::PostgresConnection(const PostgresClientParams &p)
    : PostgresClient(p, true, acquire_conn_id())
{
    if (valid_uri) {
        conn_ = std::make_unique<wfpg::WFPostgresConnection>(static_cast<int>(conn_id));
        if (conn_->init(url) != 0) {
            conn_.reset();
        }
    }
}

PostgresConnection::PostgresConnection(std::string_view url)
    : PostgresConnection(PostgresClientParams::from_url(url))
{
}

PostgresConnection::PostgresConnection(std::string_view url, const PostgresClientParams &defaults)
    : PostgresConnection([&]() {
        PostgresClientParams p = defaults;
        PostgresClientParams parsed = PostgresClientParams::from_url(url);
        p.use_ssl = parsed.use_ssl;
        p.port = parsed.port;
        p.host = std::move(parsed.host);
        p.username = std::move(parsed.username);
        p.password = std::move(parsed.password);
        p.dbname = std::move(parsed.dbname);
        if (!parsed.application_name.empty()) {
            p.application_name = std::move(parsed.application_name);
        }
        return p;
    }())
{
}

PostgresConnection::~PostgresConnection()
{
    if (conn_) {
        conn_->deinit();
    }
    release_conn_id(conn_id);
}

PostgresClient::AwaiterType PostgresConnection::request(const std::string &query)
{
    if (!conn_ || !valid_uri) {
        return AwaiterType(nullptr);
    }

    wfpg::WFPostgresTask *task = conn_->create_query_task(query, wfpg::postgres_callback_t(nullptr));
    if (task) {
        task->set_send_timeout(params.send_timeout);
        task->set_receive_timeout(params.receive_timeout);
        task->set_keep_alive(params.keep_alive_timeout);
    }

    auto hook = [conn = conn_.get()](wfpg::WFPostgresTask *t) {
        if (conn && t && t->get_resp()) {
            conn->set_last_transaction_state(t->get_resp()->get_transaction_state());
        }
    };
    return AwaiterType(task, std::move(hook));
}

PostgresClient::AwaiterType PostgresConnection::request(
    const std::string &query,
    const std::vector<PostgresParameter> &binds)
{
    if (!conn_ || !valid_uri) {
        return AwaiterType(nullptr);
    }

    wfpg::WFPostgresTask *task = conn_->create_query_task(query, binds, wfpg::postgres_callback_t(nullptr));
    if (task) {
        task->set_send_timeout(params.send_timeout);
        task->set_receive_timeout(params.receive_timeout);
        task->set_keep_alive(params.keep_alive_timeout);
    }

    auto hook = [conn = conn_.get()](wfpg::WFPostgresTask *t) {
        if (conn && t && t->get_resp()) {
            conn->set_last_transaction_state(t->get_resp()->get_transaction_state());
        }
    };
    return AwaiterType(task, std::move(hook));
}

PostgresClient::AwaiterType PostgresConnection::disconnect()
{
    if (!conn_ || !valid_uri) {
        return AwaiterType(nullptr);
    }

    wfpg::WFPostgresTask *task = conn_->create_disconnect_task(wfpg::postgres_callback_t(nullptr));
    if (task) {
        task->set_send_timeout(params.send_timeout);
        task->set_receive_timeout(params.receive_timeout);
        task->set_keep_alive(0);
    }

    return AwaiterType(task);
}

PostgresClient::AwaiterType PostgresConnection::cancel()
{
    if (!conn_ || !valid_uri) {
        return AwaiterType(nullptr);
    }

    wfpg::WFPostgresTask *task = conn_->create_cancel_task(wfpg::postgres_callback_t(nullptr));
    return AwaiterType(task);
}

char PostgresConnection::get_last_transaction_state() const
{
    return conn_ ? conn_->get_last_transaction_state() : 'I';
}

bool PostgresConnection::in_transaction() const
{
    return conn_ && conn_->in_transaction();
}

bool PostgresConnection::is_transaction_failed() const
{
    return conn_ && conn_->is_transaction_failed();
}

int32_t PostgresConnection::get_backend_pid() const
{
    return conn_ ? conn_->get_backend_pid() : 0;
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
