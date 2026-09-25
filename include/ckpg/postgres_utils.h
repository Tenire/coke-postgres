#ifndef CKPG_POSTGRES_UTILS_H
#define CKPG_POSTGRES_UTILS_H

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <ctime>
#include "PostgresResult.h"
#include "postgres_client.h" // IWYU pragma: keep

namespace ckpg {

class PostgresCellView {
public:
    explicit PostgresCellView(const wfpg::protocol::PostgresCell& cell) : cell_(cell) {}

    bool is_null() const { return cell_.is_null(); }
    std::string_view raw_view() const {
        if (cell_.is_null() || cell_.data() == nullptr) return {};
        return std::string_view(static_cast<const char*>(cell_.data()), cell_.length());
    }
    std::string as_string() const { return cell_.as_string(); }
    bool as_bool() const { return cell_.as_bool(); }
    long long as_int() const { return cell_.as_int(); }
    int64_t as_bigint() const { return cell_.as_bigint(); }
    double as_double() const { return cell_.as_double(); }
    float as_float() const { return cell_.as_float(); }
    std::string as_datetime_string() const { return cell_.as_datetime_string(); }
    std::string as_date_string() const { return cell_.as_date_string(); }
    std::string as_time_string() const { return cell_.as_time_string(); }
    bool as_date(struct tm *tm) const { return cell_.as_date(tm); }
    bool as_time(struct tm *tm, int *usec = nullptr) const { return cell_.as_time(tm, usec); }
    bool as_datetime(struct tm *tm, int *usec = nullptr) const { return cell_.as_datetime(tm, usec); }
    std::string as_jsonb_string() const { return cell_.as_jsonb_string(); }
    std::string as_uuid_string() const { return cell_.as_uuid_string(); }
    std::vector<wfpg::protocol::PostgresCell> as_array() const { return cell_.as_array(); }
    std::vector<uint8_t> as_bytea() const { return cell_.as_bytea(); }
    std::vector<std::string> as_string_array() const { return cell_.as_string_array(); }
    std::vector<int64_t> as_bigint_array() const { return cell_.as_bigint_array(); }

    const wfpg::protocol::PostgresField* field() const { return cell_.field(); }
    const wfpg::protocol::PostgresCell& cell() const { return cell_; }
private:
    wfpg::protocol::PostgresCell cell_;
};

class PostgresResultSetView {
public:
    explicit PostgresResultSetView(wfpg::protocol::PostgresResponse *resp)
        : resp_(resp) {
        if (resp_) cursor_.emplace(resp);
    }
    explicit PostgresResultSetView(wfpg::protocol::PostgresResponse &resp)
        : resp_(&resp) {
        cursor_.emplace(&resp);
    }
    explicit PostgresResultSetView(PostgresResult &result)
        : resp_(result.resp) {
        if (resp_) cursor_.emplace(resp_);
    }

    bool is_ok() const { return resp_ && !resp_->is_error(); }
    bool is_error() const { return !resp_ || resp_->is_error(); }
    
    int get_field_count() const { return cursor_ ? cursor_->get_field_count() : 0; }
    const std::string& get_command_tag() const {
        static const std::string empty;
        return cursor_ ? cursor_->get_command_tag() : empty;
    }
    unsigned long long get_affected_rows() const {
        return cursor_ ? cursor_->get_affected_rows() : 0;
    }
    unsigned long long get_insert_oid() const {
        return cursor_ ? cursor_->get_insert_oid() : 0;
    }
    const std::vector<wfpg::protocol::PostgresField>& get_fields() const {
        static const std::vector<wfpg::protocol::PostgresField> empty;
        return cursor_ ? cursor_->get_fields() : empty;
    }
    bool next_result_set() {
        return cursor_ ? cursor_->next_result_set() : false;
    }
    bool next_row(std::vector<PostgresCellView> &cells) {
        cells.clear();

        if (!cursor_) return false;

        std::vector<wfpg::protocol::PostgresCell> row;
        if (cursor_->fetch_row(row)) {
            cells.reserve(row.size());
            for (const auto& c : row) {
                cells.emplace_back(c);
            }
            return true;
        }
        return false;
    }
    std::optional<wfpg::protocol::PostgresResultCursor>& cursor() { return cursor_; }
    const std::optional<wfpg::protocol::PostgresResultCursor>& cursor() const { return cursor_; }
private:
    wfpg::protocol::PostgresResponse *resp_;
    std::optional<wfpg::protocol::PostgresResultCursor> cursor_;
};

} // namespace ckpg

#endif
