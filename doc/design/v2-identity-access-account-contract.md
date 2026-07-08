# NavCaster v2 Identity and Access Account Contract

更新时间：2026-07-03
任务：NC-117 v2-identity-access-contract
状态：契约草案，作为 NC-118 至 NC-121 的输入
适用范围：v2 Web 用户账号、接入账号、Web session、PostgreSQL schema、Redis auth projection、API、Web IA、QA gate

## 1. 冻结结论

```text
v2 账号体系拆成两层：

Account
  Web 登录主体，代表个人或管理员实体。
  只用于 Web/AdminService 权限和操作审计。

AccessAccount
  Caster 接入凭证，代表设备、客户端或基站使用的 Basic Auth 凭证。
  只用于设备/客户端/基站接入平台，不用于登录 Web。
  永远归属于一个 Account。
```

本轮先实现：

```text
role: admin / user
用户账号可由管理员创建，也可自助注册。
自助注册默认 active。
管理员可以创建、停用、恢复、删除用户账号。
用户可以创建、启用、停用、删除自己的接入账号。
管理员不代普通用户创建 AccessAccount；管理员若创建 AccessAccount，owner 是管理员自身。
Account 和 AccessAccount 均软删除。
删除后的用户名永久不可复用。
PostgreSQL 是长期权威事实，Redis 只做 Caster auth projection、在线 TTL 状态和通知。
```

明确不做：

```text
supplier。
计费、套餐、支付、供应商结算。
邮箱/手机验证闭环。
生产级多副本 session/token。
旧 HTTP API、旧 Redis key、旧 Web 风格兼容。
```

## 2. 权限边界

| 主体 | Account 能力 | AccessAccount 能力 |
| --- | --- | --- |
| anonymous | 自助注册、登录 | 无 |
| user | 查看/更新自身 profile，退出登录 | CRUD 自己的 AccessAccount |
| admin | 创建、查询、停用、恢复、删除 Account；全局审计 | 全局只读查询和踢线；CRUD 自己的 AccessAccount |

强约束：

```text
/api/v1/me/* 的 owner_account_id 必须来自 Web session。
客户端请求体中的 owner_account_id、role、created_by、projection_version 不可信。
admin API 必须显式 target ID。
所有写操作必须写 audit_logs。
Web token 不可用于 Caster 接入认证。
Caster 接入认证只使用 AccessAccount projection。
```

## 3. PostgreSQL Schema

字段类型优先使用 v2 既有 lower_snake_case。ID 可以继续沿用 v2 字符串前缀，也可以用 UUID；NC-118 必须在实现前统一。本文用 `TEXT` 前缀 ID 表达 v2 当前风格。

### 3.1 accounts

```sql
CREATE TABLE IF NOT EXISTS accounts (
    account_id TEXT PRIMARY KEY,
    username TEXT NOT NULL,
    username_norm TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    email TEXT,
    phone TEXT,
    role TEXT NOT NULL DEFAULT 'user',
    status TEXT NOT NULL DEFAULT 'active',
    password_hash TEXT NOT NULL,
    password_algo TEXT NOT NULL DEFAULT 'argon2id',
    password_params JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_via TEXT NOT NULL DEFAULT 'admin',
    created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    disabled_at TIMESTAMPTZ,
    deleted_at TIMESTAMPTZ,
    CONSTRAINT account_role_name CHECK (role IN ('admin', 'user')),
    CONSTRAINT account_status_name CHECK (status IN ('active', 'disabled', 'deleted', 'pending_verification')),
    CONSTRAINT account_created_via_name CHECK (created_via IN ('admin', 'self_service')),
    CONSTRAINT account_username_norm_not_empty CHECK (length(username_norm) > 0),
    CONSTRAINT account_deleted_has_deleted_at CHECK (status <> 'deleted' OR deleted_at IS NOT NULL)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_accounts_username_norm ON accounts(username_norm);
CREATE INDEX IF NOT EXISTS idx_accounts_role_status ON accounts(role, status);
CREATE INDEX IF NOT EXISTS idx_accounts_created_at ON accounts(created_at DESC);
```

说明：

```text
username_norm = lower(trim(username))，实现可用 generated column 或 service 层归一化。
status=disabled 禁止 Web 登录，但保留历史事实。
status=deleted 禁止 Web 登录，用户名不可复用。
pending_verification 仅为后续验证流程预留；本轮自助注册默认 active。
```

### 3.2 account_name_locks

```sql
CREATE TABLE IF NOT EXISTS account_name_locks (
    username_norm TEXT PRIMARY KEY,
    account_id TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    first_registered_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    locked_reason TEXT NOT NULL DEFAULT 'registered',
    CONSTRAINT account_name_locks_reason_name CHECK (locked_reason IN ('registered', 'deleted', 'reserved'))
);
```

用途：

```text
创建 Account 时必须在同一 transaction 中先插入 account_name_locks。
如果 username_norm 已存在，创建失败 409 username_reserved。
删除 Account 时不得删除 account_name_locks；可把 locked_reason 更新为 deleted。
即使 accounts 未来被物理归档，account_name_locks 仍保留用户名不可复用事实。
```

### 3.3 access_accounts

```sql
CREATE TABLE IF NOT EXISTS access_accounts (
    access_account_id TEXT PRIMARY KEY,
    owner_account_id TEXT NOT NULL REFERENCES accounts(account_id),
    username TEXT NOT NULL,
    username_norm TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'active',
    password_hash TEXT NOT NULL,
    password_algo TEXT NOT NULL DEFAULT 'argon2id',
    password_params JSONB NOT NULL DEFAULT '{}'::jsonb,
    expires_at TIMESTAMPTZ,
    concurrency_limit INTEGER NOT NULL DEFAULT 1,
    auth_policy JSONB NOT NULL DEFAULT '{}'::jsonb,
    projection_version BIGINT NOT NULL DEFAULT 0,
    created_by TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    disabled_at TIMESTAMPTZ,
    deleted_at TIMESTAMPTZ,
    CONSTRAINT access_account_status_name CHECK (status IN ('active', 'disabled', 'deleted')),
    CONSTRAINT access_account_concurrency_non_negative CHECK (concurrency_limit >= 0),
    CONSTRAINT access_account_username_norm_not_empty CHECK (length(username_norm) > 0),
    CONSTRAINT access_account_deleted_has_deleted_at CHECK (status <> 'deleted' OR deleted_at IS NOT NULL)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_access_accounts_username_norm ON access_accounts(username_norm);
CREATE INDEX IF NOT EXISTS idx_access_accounts_owner_status ON access_accounts(owner_account_id, status);
CREATE INDEX IF NOT EXISTS idx_access_accounts_updated_at ON access_accounts(updated_at DESC);
```

说明：

```text
status=disabled 时不能新接入，但可恢复。
status=deleted 时不能恢复，用户名不可复用。
concurrency_limit=0 表示不限量。
auth_policy 预留 IP allow/deny、mount group、扩展权限等字段；本轮不引入 supplier。
```

### 3.4 access_name_locks

```sql
CREATE TABLE IF NOT EXISTS access_name_locks (
    username_norm TEXT PRIMARY KEY,
    access_account_id TEXT REFERENCES access_accounts(access_account_id) ON DELETE SET NULL,
    owner_account_id TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    first_registered_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    locked_reason TEXT NOT NULL DEFAULT 'registered',
    CONSTRAINT access_name_locks_reason_name CHECK (locked_reason IN ('registered', 'deleted', 'reserved'))
);
```

用途：

```text
创建 AccessAccount 时必须在同一 transaction 中先插入 access_name_locks。
如果 username_norm 已存在，创建失败 409 access_username_reserved。
删除 AccessAccount 时不得删除 access_name_locks；可把 locked_reason 更新为 deleted。
```

### 3.5 web_sessions

```sql
CREATE TABLE IF NOT EXISTS web_sessions (
    session_id TEXT PRIMARY KEY,
    account_id TEXT NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active',
    issued_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    last_seen_at TIMESTAMPTZ,
    user_agent TEXT NOT NULL DEFAULT '',
    ip_address TEXT NOT NULL DEFAULT '',
    CONSTRAINT web_session_status_name CHECK (status IN ('active', 'revoked', 'expired')),
    CONSTRAINT web_session_expiry_after_issue CHECK (expires_at > issued_at)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_web_sessions_token_hash ON web_sessions(token_hash);
CREATE INDEX IF NOT EXISTS idx_web_sessions_account_status ON web_sessions(account_id, status);
CREATE INDEX IF NOT EXISTS idx_web_sessions_expires_at ON web_sessions(expires_at);
```

要求：

```text
API 只返回明文 token 一次。
PG 只存 token_hash。
disabled/deleted Account 的 active session 必须撤销或查询时拒绝。
```

### 3.6 audit_logs

```sql
CREATE TABLE IF NOT EXISTS audit_logs (
    audit_id TEXT PRIMARY KEY,
    actor_account_id TEXT REFERENCES accounts(account_id) ON DELETE SET NULL,
    actor_username TEXT NOT NULL DEFAULT '',
    action TEXT NOT NULL,
    target_type TEXT NOT NULL,
    target_id TEXT NOT NULL DEFAULT '',
    before_data JSONB,
    after_data JSONB,
    request_id TEXT NOT NULL DEFAULT '',
    ip_address TEXT NOT NULL DEFAULT '',
    user_agent TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_audit_logs_created_at ON audit_logs(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_audit_logs_actor ON audit_logs(actor_account_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_audit_logs_target ON audit_logs(target_type, target_id, created_at DESC);
```

最小 action：

```text
auth.register
auth.login
auth.logout
account.create
account.update
account.status.update
account.password.reset
account.delete
access_account.create
access_account.update
access_account.status.update
access_account.password.update
access_account.delete
access_account.kick
```

## 4. 状态机

### 4.1 Account

```text
active -> disabled
disabled -> active
active -> deleted
disabled -> deleted
pending_verification -> active
pending_verification -> deleted
```

禁止：

```text
deleted -> active
deleted -> disabled
deleted -> pending_verification
修改 username
删除 account_name_locks
```

删除 Account 的事务规则：

```text
1. 校验 actor 是 admin，或后续产品明确允许自助注销。
2. 将 accounts.status 更新为 deleted，写 deleted_at。
3. 将 account_name_locks.locked_reason 更新为 deleted。
4. 将该 account 下 active/disabled AccessAccount 全部更新为 disabled。
5. 撤销 web_sessions active session。
6. 写 audit_logs account.delete 和 access_account.status.update 摘要。
7. 触发 affected AccessAccount auth projection 删除或 disabled projection。
```

### 4.2 AccessAccount

```text
active -> disabled
disabled -> active
active -> deleted
disabled -> deleted
```

禁止：

```text
deleted -> active
deleted -> disabled
修改 username
修改 owner_account_id
删除 access_name_locks
```

停用 AccessAccount：

```text
status=disabled
disabled_at=now()
projection_version += 1
删除或更新 auth:access-account:<username_norm> 为 disabled
新连接必须拒绝
```

删除 AccessAccount：

```text
status=deleted
deleted_at=now()
projection_version += 1
access_name_locks.locked_reason=deleted
删除 auth:access-account:<username_norm> 或写 disabled/deleted projection
新连接必须拒绝
```

即时踢线：

```text
本轮设计要求停用/删除后最终踢掉当前连接。
NC-118/NC-119 首轮最低实现是 projection 刷新后拒绝新连接。
如果当前连接即时踢线超出首轮实现，应在 NC-119 或后续拆出 explicit kick 任务，不能从设计中删除该要求。
```

## 5. API Contract Summary

完整 API 细节见 `doc/api/v2-identity-access-api.md`。

必需 endpoint：

```text
POST /api/v1/auth/register
POST /api/v1/auth/login
POST /api/v1/auth/logout
GET  /api/v1/auth/session

GET    /api/v1/admin/accounts
POST   /api/v1/admin/accounts
GET    /api/v1/admin/accounts/{account_id}
PUT    /api/v1/admin/accounts/{account_id}
PUT    /api/v1/admin/accounts/{account_id}/status
PUT    /api/v1/admin/accounts/{account_id}/password
DELETE /api/v1/admin/accounts/{account_id}
GET    /api/v1/admin/access-accounts

GET    /api/v1/me/profile
PUT    /api/v1/me/profile
GET    /api/v1/me/access-accounts
POST   /api/v1/me/access-accounts
GET    /api/v1/me/access-accounts/{access_account_id}
PUT    /api/v1/me/access-accounts/{access_account_id}
PUT    /api/v1/me/access-accounts/{access_account_id}/password
PUT    /api/v1/me/access-accounts/{access_account_id}/status
DELETE /api/v1/me/access-accounts/{access_account_id}
```

错误码：

```text
invalid_request
unauthorized
forbidden
not_found
username_reserved
access_username_reserved
account_disabled
account_deleted
access_account_disabled
access_account_deleted
invalid_state_transition
projection_unavailable
```

## 6. Redis Auth Projection

Key：

```text
auth:access-account:<username_norm>
auth:policy:<access_account_id>
auth:version
control:kick
```

`auth:access-account:<username_norm>` JSON：

```json
{
  "access_account_id": "aacc_01J...",
  "username": "field-rover-a01",
  "username_norm": "field-rover-a01",
  "status": "active",
  "owner_account_id": "acc_01J...",
  "owner_status": "active",
  "password_hash": "argon2id...",
  "password_algo": "argon2id",
  "password_params": {},
  "expires_at": null,
  "concurrency_limit": 1,
  "auth_policy": {},
  "projection_version": 42,
  "updated_at": "2026-07-03T00:00:00Z"
}
```

Projection 规则：

```text
Account active + AccessAccount active -> 写 active projection。
Account disabled/deleted -> 删除其名下 AccessAccount projection 或写 disabled projection。
AccessAccount disabled/deleted -> 删除 projection 或写 disabled/deleted projection。
删除 key 与写 disabled projection 二选一，但 Caster 行为必须一致：拒绝新连接。
auth:version 每次成功投影后单调递增或记录最新 projection_version。
```

Caster 读取规则：

```text
cache hit -> 使用本地缓存校验。
cache miss -> GET auth:access-account:<username_norm>。
missing -> reject unknown_access_account。
status != active -> reject access_account_disabled。
owner_status != active -> reject owner_account_disabled。
expires_at < now -> reject access_account_expired。
密码不匹配 -> reject invalid_credentials。
```

禁止：

```text
Caster 登录热路径访问 PostgreSQL。
Caster 登录热路径同步调用 AdminService。
Web token 参与 Caster Basic Auth。
```

## 7. Web IA

新增页面：

```text
/login
/register
/admin/control/users
/admin/control/access-accounts
/admin/control/audit
/access-accounts
/profile
```

角色入口：

```text
anonymous:
  /login
  /register

user:
  /access-accounts
  /profile

admin:
  /admin/control/users
  /admin/control/access-accounts
  /admin/control/audit
  /access-accounts
  /profile
```

页面最低能力：

```text
登录页：用户名、密码、错误态。
注册页：用户名、显示名、密码、确认密码、重名错误态。
管理员用户页：列表、创建、启用/停用、删除、密码重置。
管理员接入账号页：全局只读查询、按 owner/status 搜索，后续踢线入口。
用户接入账号页：列表、创建、启用/停用、删除、改密码。
个人资料页：显示 username、display_name、role、status。
```

## 8. NC-118 至 NC-121 输入

### NC-118 AdminService

必须实现：

```text
PG migration。
repository/service。
auth/session middleware。
password hashing。
name_locks transaction。
audit write。
projection update hook。
API tests and permission negative tests。
```

### NC-119 Caster

必须实现或确认：

```text
AccessAccount projection JSON 读取。
disabled/deleted/missing rejection。
owner disabled/deleted rejection。
no PG/AdminService hot path。
auth smoke。
```

### NC-120 Web

必须实现：

```text
login/register route。
session guard。
admin users page。
admin access accounts read page。
user access accounts CRUD page。
real API client and no mock pass condition。
```

### NC-121 Integration / QA / Review

必须覆盖：

```text
empty PG migration。
self register -> login -> session。
admin create/disable/enable/delete user。
user create/disable/enable/delete access account。
name lock: deleted user username cannot be reused。
name lock: deleted access username cannot be reused。
user cannot access another user's AccessAccount。
disabled/deleted AccessAccount rejected by Caster new login。
Web browser smoke。
audit records for critical writes。
```

## 9. 最低验证命令

```powershell
cd app\admin
go test ./...
go build ./cmd/navcaster-admin

cd app\caster
cmake --build <configured-build> --target navcaster-caster

cd app\web
npm run build
```

Integration smoke must use:

```text
real PostgreSQL
real Redis
real navcaster-admin
real navcaster-caster
real app/web browser smoke
```

Mock-only verification cannot mark NC-121 as QA_PASSED.
