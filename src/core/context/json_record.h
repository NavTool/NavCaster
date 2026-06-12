#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace navcaster::json_record
{

inline std::string dump_record(const nlohmann::json &record)
{
    return record.dump();
}

inline bool parse_record(std::string_view text, nlohmann::json &out, std::string *error = nullptr)
{
    try
    {
        out = nlohmann::json::parse(text.begin(), text.end());
        return true;
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = e.what();
        }
        out = nullptr;
        return false;
    }
}

inline bool coerce_record(nlohmann::json value, nlohmann::json &out, std::string *error = nullptr)
{
    if (value.is_string())
    {
        nlohmann::json parsed;
        if (!parse_record(value.get<std::string>(), parsed, error))
        {
            out = nullptr;
            return false;
        }
        value = std::move(parsed);
    }
    if (!value.is_object())
    {
        if (error)
        {
            *error = "JSON record is not an object";
        }
        out = nullptr;
        return false;
    }
    out = std::move(value);
    return true;
}

inline std::int64_t as_i64(const nlohmann::json &value, std::int64_t fallback = 0)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return value.get<std::int64_t>();
    }
    if (value.is_number_float())
    {
        return static_cast<std::int64_t>(value.get<double>());
    }
    return fallback;
}

inline int as_int(const nlohmann::json &value, int fallback = 0)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return value.get<int>();
    }
    if (value.is_number_float())
    {
        return static_cast<int>(value.get<double>());
    }
    return fallback;
}

inline int bool_or_number_as_int(const nlohmann::json &value, int fallback = 0)
{
    if (value.is_boolean())
    {
        return value.get<bool>() ? 1 : 0;
    }
    return as_int(value, fallback);
}

inline std::string string_field(const nlohmann::json &record, const char *field, std::string fallback = {})
{
    auto it = record.find(field);
    if (it == record.end() || !it->is_string())
    {
        return fallback;
    }
    return it->get<std::string>();
}

inline bool has_nonempty_string_field(const nlohmann::json &record, const char *field)
{
    return !string_field(record, field).empty();
}

inline void touch_timestamps(nlohmann::json &record,
                             std::int64_t now,
                             const char *create_field = "create_time",
                             const char *update_field = "update_time")
{
    auto create_it = record.find(create_field);
    if (create_it == record.end() || as_i64(*create_it) <= 0)
    {
        record[create_field] = now;
    }
    record[update_field] = now;
}

} // namespace navcaster::json_record
