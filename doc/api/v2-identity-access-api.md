# v2 Identity and Access Account API

更新时间：2026-07-03
任务：NC-117 v2-identity-access-contract
状态：契约草案，供 NC-118/NC-120 实现

## 1. 通用规范

响应 envelope：

```json
{
  "request_id": "req_01J...",
  "data": {}
}
```

错误 envelope：

```json
{
  "request_id": "req_01J...",
  "error": {
    "code": "username_reserved",
    "message": "username is already reserved",
    "details": {}
  }
}
```

认证：

```text
Authorization: Bearer <opaque_token>
```

token 只返回一次，服务端只保存 token_hash。

## 2. Auth API

### POST /api/v1/auth/register

公开接口。自助注册普通用户账号，默认 `role=user`、`status=active`。

Request：

```json
{
  "username": "alice",
  "password": "plain_password",
  "display_name": "Alice"
}
```

Response 201：

```json
{
  "request_id": "req_01J...",
  "data": {
    "account_id": "acc_01J...",
    "username": "alice",
    "display_name": "Alice",
    "role": "user",
    "status": "active",
    "created_via": "self_service"
  }
}
```

Errors：

| HTTP | code | 说明 |
| --- | --- | --- |
| 400 | invalid_request | username/password 缺失或格式错误 |
| 409 | username_reserved | username_norm 已在 account_name_locks 中存在 |

### POST /api/v1/auth/login

Request：

```json
{
  "username": "alice",
  "password": "plain_password"
}
```

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": {
    "token": "opaque_token",
    "session_id": "sess_01J...",
    "account": {
      "account_id": "acc_01J...",
      "username": "alice",
      "display_name": "Alice",
      "role": "user",
      "status": "active"
    },
    "expires_at": "2026-07-04T00:00:00Z"
  }
}
```

Errors：

| HTTP | code | 说明 |
| --- | --- | --- |
| 401 | unauthorized | 用户名或密码错误 |
| 403 | account_disabled | 账号 disabled |
| 403 | account_deleted | 账号 deleted |

### POST /api/v1/auth/logout

需要 Bearer token。撤销当前 session。

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": {
    "ok": true
  }
}
```

### GET /api/v1/auth/session

需要 Bearer token。

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": {
    "session_id": "sess_01J...",
    "account_id": "acc_01J...",
    "username": "alice",
    "display_name": "Alice",
    "role": "user",
    "status": "active",
    "expires_at": "2026-07-04T00:00:00Z"
  }
}
```

## 3. Admin Account API

所有 `/api/v1/admin/*` 需要 `role=admin`。

### GET /api/v1/admin/accounts

Query：

```text
status=active|disabled|deleted
role=admin|user
search=<username/display_name>
limit=50
offset=0
```

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": [
    {
      "account_id": "acc_01J...",
      "username": "alice",
      "display_name": "Alice",
      "role": "user",
      "status": "active",
      "created_via": "self_service",
      "created_at": "2026-07-03T00:00:00Z",
      "updated_at": "2026-07-03T00:00:00Z"
    }
  ],
  "page": {
    "limit": 50,
    "offset": 0,
    "total": 1
  }
}
```

### POST /api/v1/admin/accounts

Request：

```json
{
  "username": "bob",
  "password": "plain_password",
  "display_name": "Bob",
  "role": "user",
  "status": "active"
}
```

Response 201：Account summary.

Errors：

| HTTP | code | 说明 |
| --- | --- | --- |
| 409 | username_reserved | 用户名已被使用或历史删除后保留 |

### GET /api/v1/admin/accounts/{account_id}

Response 200：Account detail，包含统计摘要：

```json
{
  "request_id": "req_01J...",
  "data": {
    "account_id": "acc_01J...",
    "username": "alice",
    "display_name": "Alice",
    "role": "user",
    "status": "active",
    "access_account_count": 2,
    "active_session_count": 1,
    "created_at": "2026-07-03T00:00:00Z",
    "updated_at": "2026-07-03T00:00:00Z"
  }
}
```

### PUT /api/v1/admin/accounts/{account_id}

不可修改 `username`、`role` 除非 NC-118 明确实现 role migration。首轮只允许：

```json
{
  "display_name": "Alice Zhang",
  "email": "alice@example.com",
  "phone": ""
}
```

### PUT /api/v1/admin/accounts/{account_id}/status

Request：

```json
{
  "status": "disabled",
  "reason": "manual review"
}
```

允许：

```text
active <-> disabled
active|disabled -> deleted
```

`deleted` 建议使用 `DELETE` endpoint；该 endpoint 可以作为实现复用。

### PUT /api/v1/admin/accounts/{account_id}/password

管理员重置用户密码。

Request：

```json
{
  "password": "new_plain_password",
  "revoke_sessions": true
}
```

### DELETE /api/v1/admin/accounts/{account_id}

软删除 Account：

```text
accounts.status=deleted
accounts.deleted_at=now()
account_name_locks.locked_reason=deleted
名下 access_accounts status=disabled
active web_sessions revoked
auth projection refreshed
audit written
```

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": {
    "account_id": "acc_01J...",
    "status": "deleted",
    "disabled_access_account_count": 2
  }
}
```

### GET /api/v1/admin/access-accounts

全局只读查询。管理员不代普通用户创建 AccessAccount。

Query：

```text
owner_account_id=acc_...
status=active|disabled|deleted
search=<username/display_name>
limit=50
offset=0
```

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": [
    {
      "access_account_id": "aacc_01J...",
      "owner_account_id": "acc_01J...",
      "owner_username": "alice",
      "username": "field-rover-a01",
      "display_name": "Rover A01",
      "status": "active",
      "concurrency_limit": 1,
      "expires_at": null,
      "created_at": "2026-07-03T00:00:00Z",
      "updated_at": "2026-07-03T00:00:00Z"
    }
  ]
}
```

## 4. Me API

所有 `/api/v1/me/*` 需要已登录 Account，`owner_account_id` 从 session 推导。

### GET /api/v1/me/profile

Response 200：当前 Account 脱敏资料。

### PUT /api/v1/me/profile

Request：

```json
{
  "display_name": "Alice Zhang",
  "email": "alice@example.com",
  "phone": ""
}
```

### GET /api/v1/me/access-accounts

Response 200：当前用户自己的 AccessAccount 列表。

### POST /api/v1/me/access-accounts

Request：

```json
{
  "username": "field-rover-a01",
  "password": "plain_password",
  "display_name": "Rover A01",
  "note": "field team",
  "concurrency_limit": 1,
  "expires_at": null
}
```

服务端覆盖：

```text
owner_account_id = session.account_id
status = active
created_by = session.account_id
projection_version = next
```

Response 201：AccessAccount summary，不返回密码。

Errors：

| HTTP | code | 说明 |
| --- | --- | --- |
| 409 | access_username_reserved | 接入账号名已被使用或历史删除后保留 |
| 403 | account_disabled | owner Account 不可用 |

### GET /api/v1/me/access-accounts/{access_account_id}

只能查询自己的 AccessAccount。非 owner 返回 404 或 403；NC-118 必须选定并测试。

### PUT /api/v1/me/access-accounts/{access_account_id}

允许字段：

```json
{
  "display_name": "Rover A01",
  "note": "updated note",
  "concurrency_limit": 2,
  "expires_at": null
}
```

禁止修改：

```text
username
owner_account_id
status
password_hash
projection_version
```

### PUT /api/v1/me/access-accounts/{access_account_id}/password

Request：

```json
{
  "password": "new_plain_password"
}
```

成功后：

```text
password_hash updated
projection_version bumped
auth projection refreshed
audit written
```

### PUT /api/v1/me/access-accounts/{access_account_id}/status

Request：

```json
{
  "status": "disabled",
  "reason": "temporary stop"
}
```

允许：

```text
active <-> disabled
```

删除使用 DELETE。

### DELETE /api/v1/me/access-accounts/{access_account_id}

软删除自己的 AccessAccount。

Response 200：

```json
{
  "request_id": "req_01J...",
  "data": {
    "access_account_id": "aacc_01J...",
    "status": "deleted"
  }
}
```

删除后同名创建必须返回：

```json
{
  "request_id": "req_01J...",
  "error": {
    "code": "access_username_reserved",
    "message": "access account username is reserved"
  }
}
```

## 5. Projection Events

任何影响 Caster 登录的新写操作必须刷新 projection：

```text
access_account.create
access_account.status.update
access_account.password.update
access_account.delete
account.status.update
account.delete
```

Projection failure response:

```text
如果 PG 事务已提交但 Redis projection 失败：
  API 可以返回 202/200 并标记 projection_status=failed，或返回 503 并保留可重试 outbox。
  NC-118 必须选择一种策略并测试。
  不允许 PG 半写后无审计、无重试路径。
```

建议 response meta：

```json
{
  "request_id": "req_01J...",
  "data": {},
  "meta": {
    "projection_status": "projected|pending|failed"
  }
}
```

## 6. Required Negative Tests

```text
anonymous GET /api/v1/me/profile -> 401
user GET /api/v1/admin/accounts -> 403
user GET another user's access account -> 404 or 403
user PUT another user's access account -> 404 or 403
register deleted username -> 409 username_reserved
create deleted access username -> 409 access_username_reserved
login disabled account -> 403 account_disabled
login deleted account -> 403 account_deleted
create access account when owner disabled -> 403 account_disabled
disabled/deleted AccessAccount Caster login -> rejected
```
