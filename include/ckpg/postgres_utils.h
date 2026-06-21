#ifndef CKPG_POSTGRES_UTILS_H
#define CKPG_POSTGRES_UTILS_H

#include <string>
#include <string_view>
#include <vector>
#include <optional>

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

private:
    wfpg::protocol::PostgresCell cell_;
};

class PostgresResultSetView {
public:
    explicit PostgresResultSetView(wfpg::protocol::PostgresResponse *resp)
        : resp_(resp) {
        if (resp_) cursor_.emplace(resp);
    }

    bool is_ok() const { return resp_ && !resp_->is_error(); }
    bool is_error() const { return !resp_ || resp_->is_error(); }
    
    int get_field_count() const { return cursor_ ? cursor_->get_field_count() : 0; }
    
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

private:
    wfpg::protocol::PostgresResponse *resp_;
    std::optional<wfpg::protocol::PostgresResultCursor> cursor_;
};

} // namespace ckpg

#endif
