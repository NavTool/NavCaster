#include "account_schema.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace navcaster::account_schema
{
namespace
{
constexpr int ACCOUNT_STATE_TYPE_NORMAL = 1;
constexpr int ACCOUNT_ACTIVE_STATE_ACTIVE = 1;
constexpr std::size_t SHA256_BLOCK_SIZE = 64;
constexpr std::size_t SHA256_DIGEST_SIZE = 32;

using ByteVector = std::vector<std::uint8_t>;

constexpr std::array<std::uint32_t, 64> SHA256_K = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

std::uint32_t rotr(std::uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

std::uint32_t load_be32(const std::uint8_t *data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) |
           static_cast<std::uint32_t>(data[3]);
}

void store_be32(ByteVector &out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

ByteVector string_to_bytes(const std::string &value)
{
    return ByteVector(value.begin(), value.end());
}

ByteVector sha256(ByteVector data)
{
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % SHA256_BLOCK_SIZE) != 56)
    {
        data.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        data.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
    }

    std::array<std::uint32_t, 8> h = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    for (std::size_t offset = 0; offset < data.size(); offset += SHA256_BLOCK_SIZE)
    {
        std::array<std::uint32_t, 64> w = {};
        for (std::size_t i = 0; i < 16; ++i)
        {
            w[i] = load_be32(data.data() + offset + i * 4);
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];

        for (std::size_t i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = hh + s1 + ch + SHA256_K[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    ByteVector digest;
    digest.reserve(SHA256_DIGEST_SIZE);
    for (const auto word : h)
    {
        store_be32(digest, word);
    }
    return digest;
}

ByteVector hmac_sha256(ByteVector key, const ByteVector &message)
{
    if (key.size() > SHA256_BLOCK_SIZE)
    {
        key = sha256(std::move(key));
    }
    key.resize(SHA256_BLOCK_SIZE, 0U);

    ByteVector inner_key(SHA256_BLOCK_SIZE);
    ByteVector outer_key(SHA256_BLOCK_SIZE);
    for (std::size_t i = 0; i < SHA256_BLOCK_SIZE; ++i)
    {
        inner_key[i] = static_cast<std::uint8_t>(key[i] ^ 0x36U);
        outer_key[i] = static_cast<std::uint8_t>(key[i] ^ 0x5cU);
    }

    ByteVector inner;
    inner.reserve(SHA256_BLOCK_SIZE + message.size());
    inner.insert(inner.end(), inner_key.begin(), inner_key.end());
    inner.insert(inner.end(), message.begin(), message.end());
    auto inner_digest = sha256(std::move(inner));

    ByteVector outer;
    outer.reserve(SHA256_BLOCK_SIZE + inner_digest.size());
    outer.insert(outer.end(), outer_key.begin(), outer_key.end());
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return sha256(std::move(outer));
}

ByteVector pbkdf2_sha256(const std::string &password, const std::string &salt, int iterations, std::size_t output_size)
{
    if (iterations <= 0 || output_size == 0)
    {
        return {};
    }

    const auto key = string_to_bytes(password);
    const auto salt_bytes = string_to_bytes(salt);
    const std::uint32_t blocks = static_cast<std::uint32_t>((output_size + SHA256_DIGEST_SIZE - 1) / SHA256_DIGEST_SIZE);
    ByteVector output;
    output.reserve(blocks * SHA256_DIGEST_SIZE);

    for (std::uint32_t block = 1; block <= blocks; ++block)
    {
        ByteVector message = salt_bytes;
        store_be32(message, block);

        auto u = hmac_sha256(key, message);
        ByteVector t = u;
        for (int i = 1; i < iterations; ++i)
        {
            u = hmac_sha256(key, u);
            for (std::size_t j = 0; j < t.size(); ++j)
            {
                t[j] = static_cast<std::uint8_t>(t[j] ^ u[j]);
            }
        }
        output.insert(output.end(), t.begin(), t.end());
    }

    output.resize(output_size);
    return output;
}

std::string hex_encode(const ByteVector &bytes)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const auto byte : bytes)
    {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string generate_password_salt()
{
    std::array<std::uint8_t, 16> salt = {};
    std::random_device random;
    for (auto &byte : salt)
    {
        byte = static_cast<std::uint8_t>(random() & 0xffU);
    }
    return hex_encode(ByteVector(salt.begin(), salt.end()));
}

bool constant_time_equal(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    unsigned char diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        diff = static_cast<unsigned char>(diff | (static_cast<unsigned char>(lhs[i]) ^ static_cast<unsigned char>(rhs[i])));
    }
    return diff == 0;
}

std::int64_t number_to_i64(const nlohmann::json &value, std::int64_t fallback = 0)
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

int number_to_int(const nlohmann::json &value, int fallback = 0)
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

int bool_or_number_to_int(const nlohmann::json &value, int fallback = 0)
{
    if (value.is_boolean())
    {
        return value.get<bool>() ? 1 : 0;
    }
    return number_to_int(value, fallback);
}

std::string string_or_empty(const nlohmann::json &record, const char *field)
{
    auto it = record.find(field);
    if (it == record.end() || !it->is_string())
    {
        return {};
    }
    return it->get<std::string>();
}

bool has_nonempty_string_field(const nlohmann::json &record, const char *field)
{
    return !string_or_empty(record, field).empty();
}

bool has_valid_hash_material(const nlohmann::json &record)
{
    if (!has_nonempty_string_field(record, "password_hash"))
    {
        return false;
    }

    const std::string algo = string_or_empty(record, "password_algo");
    const std::string salt = string_or_empty(record, "password_salt");
    const auto iterations_it = record.find("password_iterations");
    const int iterations = iterations_it == record.end() ? 0 : number_to_int(*iterations_it);
    return is_supported_password_algo(algo) && !salt.empty() && iterations > 0;
}

int password_iterations_or_default(const nlohmann::json &record)
{
    const auto it = record.find("password_iterations");
    if (it == record.end())
    {
        return DEFAULT_PASSWORD_ITERATIONS;
    }
    const int iterations = number_to_int(*it, DEFAULT_PASSWORD_ITERATIONS);
    return iterations <= 0 ? DEFAULT_PASSWORD_ITERATIONS : iterations;
}
} // namespace

std::string normalize_group_uid(const std::string &group_uid)
{
    return group_uid.empty() ? DEFAULT_GROUP_UID : group_uid;
}

int normalize_connection_limit(int connection_limit)
{
    return connection_limit <= 0 ? UNLIMITED_CONNECTIONS : connection_limit;
}

std::string make_password_hash(const std::string &password, const std::string &salt, int iterations)
{
    return hex_encode(pbkdf2_sha256(password, salt, iterations, SHA256_DIGEST_SIZE));
}

bool is_supported_password_algo(const std::string &algo)
{
    return algo == PASSWORD_ALGO_PBKDF2_SHA256;
}

bool has_password_material(const nlohmann::json &record)
{
    return has_nonempty_string_field(record, "password_hash") ||
           has_nonempty_string_field(record, "password");
}

void preserve_existing_password_material(nlohmann::json &record, const nlohmann::json &existing)
{
    if (has_nonempty_string_field(record, "password"))
    {
        record.erase("password_hash");
        record.erase("password_algo");
        record.erase("password_salt");
        record.erase("password_iterations");
        return;
    }

    if (has_nonempty_string_field(record, "password_hash"))
    {
        record.erase("password");
        return;
    }

    record.erase("password");
    record.erase("password_hash");
    record.erase("password_algo");
    record.erase("password_salt");
    record.erase("password_iterations");

    if (has_nonempty_string_field(existing, "password_hash"))
    {
        record["password_hash"] = existing["password_hash"];
        if (existing.contains("password_algo"))
        {
            record["password_algo"] = existing["password_algo"];
        }
        if (existing.contains("password_salt"))
        {
            record["password_salt"] = existing["password_salt"];
        }
        if (existing.contains("password_iterations"))
        {
            record["password_iterations"] = existing["password_iterations"];
        }
        return;
    }

    if (has_nonempty_string_field(existing, "password"))
    {
        record["password"] = existing["password"];
    }
}

nlohmann::json normalize_account_record(nlohmann::json record, std::int64_t now)
{
    const std::string account = record.value("account", record.value("uid", std::string{}));
    if (!account.empty())
    {
        record["account"] = account;
        if (!record.contains("uid") || record.value("uid", std::string{}).empty())
        {
            record["uid"] = account;
        }
    }

    record["schema_version"] = record.value("schema_version", CURRENT_SCHEMA_VERSION);
    record["group_uid"] = normalize_group_uid(record.value("group_uid", record.value("group", std::string{})));
    record["connection_limit"] = record.value("connection_limit", record.value("connect_limit", 0));

    if (!record.contains("create_time") || number_to_i64(record["create_time"]) <= 0)
    {
        record["create_time"] = now;
    }
    record["update_time"] = now;

    if (has_nonempty_string_field(record, "password"))
    {
        const std::string password = record.value("password", std::string{});
        const std::string salt = has_nonempty_string_field(record, "password_salt")
                                     ? record.value("password_salt", std::string{})
                                     : generate_password_salt();
        const int iterations = password_iterations_or_default(record);
        record["password_hash"] = make_password_hash(password, salt, iterations);
        record["password_algo"] = PASSWORD_ALGO_PBKDF2_SHA256;
        record["password_salt"] = salt;
        record["password_iterations"] = iterations;
        record.erase("password");
    }
    else if (has_nonempty_string_field(record, "password_hash"))
    {
        record.erase("password");
    }
    else
    {
        record.erase("password");
    }

    return record;
}

nlohmann::json build_active_index(const nlohmann::json &record)
{
    nlohmann::json active;
    active["schema_version"] = record.value("schema_version", CURRENT_SCHEMA_VERSION);
    active["uid"] = record.value("uid", record.value("account", std::string{}));
    active["account"] = record.value("account", active.value("uid", std::string{}));
    active["group_uid"] = normalize_group_uid(record.value("group_uid", record.value("group", std::string{})));
    active["connection_limit"] = record.value("connection_limit", record.value("connect_limit", 0));
    active["type"] = record.value("type", 0);
    active["state"] = record.value("state", 0);
    active["active"] = record.value("active", 0);
    active["expire_time"] = record.value("expire_time", record.value("expire", 0.0));

    if (has_nonempty_string_field(record, "password_hash"))
    {
        active["password_hash"] = record["password_hash"];
        active["password_algo"] = record.value("password_algo", std::string{});
        if (record.contains("password_salt"))
        {
            active["password_salt"] = record["password_salt"];
        }
        if (record.contains("password_iterations"))
        {
            active["password_iterations"] = record["password_iterations"];
        }
    }
    else if (has_nonempty_string_field(record, "password"))
    {
        active["password"] = record["password"];
        active["legacy_plain_password"] = true;
    }

    return active;
}

bool build_account_sync_plan(nlohmann::json record, std::int64_t now, AccountSyncPlan &plan, std::string *error)
{
    plan = {};
    auto normalized = normalize_account_record(std::move(record), now);
    const std::string account = normalized.value("account", std::string{});
    if (account.empty())
    {
        if (error)
        {
            *error = "account is required";
        }
        return false;
    }
    if (!has_password_material(normalized))
    {
        if (error)
        {
            *error = "password is required";
        }
        return false;
    }
    if (has_nonempty_string_field(normalized, "password_hash") && !has_valid_hash_material(normalized))
    {
        if (error)
        {
            *error = "password hash is invalid";
        }
        return false;
    }

    plan.account = account;
    plan.record = std::move(normalized);

    std::string reason;
    if (is_login_enabled(plan.record, now, &reason))
    {
        plan.write_active_index = true;
        plan.active_index = build_active_index(plan.record);
    }
    else
    {
        plan.delete_active_index = true;
        plan.inactive_reason = std::move(reason);
    }

    return true;
}

bool build_account_delete_plan(const std::string &account, AccountDeletePlan &plan, std::string *error)
{
    plan = {};
    if (account.empty())
    {
        if (error)
        {
            *error = "account is required";
        }
        return false;
    }

    plan.account = account;
    plan.delete_record = true;
    plan.delete_active_index = true;
    return true;
}

bool is_login_enabled(const nlohmann::json &record, std::int64_t now, std::string *reason)
{
    const int state = record.value("state", 0);
    if (state != 0 && state != ACCOUNT_STATE_TYPE_NORMAL)
    {
        if (reason)
        {
            *reason = "account state is not normal";
        }
        return false;
    }

    const int active = record.value("active", 0);
    if (active != 0 && active != ACCOUNT_ACTIVE_STATE_ACTIVE)
    {
        if (reason)
        {
            *reason = "account is not active";
        }
        return false;
    }

    auto expire_it = record.find("expire_time");
    if (expire_it == record.end())
    {
        expire_it = record.find("expire");
    }
    const std::int64_t expire_time = expire_it == record.end() ? 0 : number_to_i64(*expire_it);
    if (expire_time > 0 && now > expire_time)
    {
        if (reason)
        {
            *reason = "account expired";
        }
        return false;
    }

    return true;
}

bool parse_auth_view(const std::string &json_text, AccountAuthView &view, std::string *error)
{
    nlohmann::json record;
    try
    {
        record = nlohmann::json::parse(json_text);
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = e.what();
        }
        return false;
    }

    view.account = record.value("account", record.value("uid", std::string{}));
    view.password = string_or_empty(record, "password");
    view.password_hash = string_or_empty(record, "password_hash");
    view.password_algo = string_or_empty(record, "password_algo");
    view.password_salt = string_or_empty(record, "password_salt");
    auto iterations_it = record.find("password_iterations");
    view.password_iterations = iterations_it == record.end() ? 0 : number_to_int(*iterations_it);
    view.group_uid = normalize_group_uid(record.value("group_uid", record.value("group", std::string{})));
    view.connection_limit = normalize_connection_limit(record.value("connection_limit", record.value("connect_limit", 0)));
    view.type = record.value("type", 0);
    auto state_it = record.find("state");
    view.state = state_it == record.end() ? 0 : bool_or_number_to_int(*state_it);
    auto active_it = record.find("active");
    view.active = active_it == record.end() ? 0 : bool_or_number_to_int(*active_it);

    auto expire_it = record.find("expire_time");
    if (expire_it == record.end())
    {
        expire_it = record.find("expire");
    }
    view.expire_time = expire_it == record.end() ? 0 : number_to_i64(*expire_it);
    view.legacy_plain_password = view.password_hash.empty() && !view.password.empty();
    return true;
}

bool password_matches(const AccountAuthView &view, const std::string &password)
{
    if (!view.password_hash.empty())
    {
        if (!is_supported_password_algo(view.password_algo) ||
            view.password_salt.empty() ||
            view.password_iterations <= 0)
        {
            return false;
        }

        return constant_time_equal(
            make_password_hash(password, view.password_salt, view.password_iterations),
            view.password_hash);
    }

    if (view.legacy_plain_password)
    {
        return view.password == password;
    }

    return false;
}

} // namespace navcaster::account_schema
