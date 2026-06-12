#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

class StatisticsService
{
public:
    nlohmann::json overview(const nlohmann::json &mpt_logs,
                            const nlohmann::json &usr_logs,
                            long long start_ts,
                            long long end_ts,
                            long long now_ts) const;

    nlohmann::json daily(const std::string &date,
                         const nlohmann::json &mpt_logs,
                         const nlohmann::json &usr_logs,
                         long long start_ts,
                         long long end_ts,
                         long long now_ts) const;

    nlohmann::json mountpoint_ranking(const nlohmann::json &mpt_logs,
                                      long long start_ts,
                                      long long end_ts,
                                      long long now_ts,
                                      int limit) const;

    nlohmann::json user_ranking(const nlohmann::json &usr_logs,
                                long long start_ts,
                                long long end_ts,
                                long long now_ts,
                                int limit) const;
};

} // namespace navcaster::http_api
