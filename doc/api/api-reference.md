# NavCaster HTTP API 接口文档

> 版本：v1.0 | 服务端口：默认 8080 | 基于 libevent evhttp

---

## 目录

- [全局规范](#全局规范)
- [认证](#1-认证)
- [运营域 API v1](#11-运营域-api-v1)
- [账户管理](#2-账户管理)
- [源表记录](#3-源表记录)
- [基站状态](#4-基站状态只读)
- [用户状态](#5-用户状态只读)
- [数据流状态](#6-数据流状态只读)
- [别名规则](#7-别名规则)
- [访问控制组](#8-访问控制组)
- [访问控制项](#9-访问控制项)
- [Pull 中继](#10-pull-中继)
- [Push 中继](#11-push-中继)
- [集群节点](#12-集群节点只读)
- [挂载点订阅者](#13-挂载点订阅者)
- [系统状态](#14-系统状态)
- [配置管理](#15-配置管理)
- [工具接口](#16-工具接口)
- [SSE 实时推送](#17-sse-实时推送)
- [错误响应](#通用错误响应)

---

## 全局规范

### 认证方式

- **Bearer Token**：服务端生成的 64 位十六进制随机字符串，非 JWT
- **请求头**：`Authorization: Bearer <token>`
- **公开路径**（无需认证）：`POST /api/auth/login`、`GET /api/status/health`
- SSE 端点支持查询参数认证：`?token=<token>`

### CORS

| Header | Value |
|--------|-------|
| `Access-Control-Allow-Origin` | 可配置（默认 `*`） |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, DELETE, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type, Authorization` |
| `Access-Control-Max-Age` | `86400` |

所有 `OPTIONS` 请求自动返回 `204 No Content`。

### 通用 CRUD 模式

大部分资源基于 Redis Hash 存储，遵循统一 CRUD 模式：

| 操作 | Method | Path | Redis | 说明 |
|------|--------|------|-------|------|
| 列表 | GET | `/api/{resource}` | `HGETALL` | 返回 `Record<string, T>` |
| 详情 | GET | `/api/{resource}/{id}` | `HGET` | 返回单个 `T` |
| 创建 | POST | `/api/{resource}` | `HSETNX` | 201 成功 / 409 冲突 |
| 更新 | PUT | `/api/{resource}/{id}` | `HSET` | 完整覆盖 |
| 删除 | DELETE | `/api/{resource}/{id}` | `HDEL` | 200 / 404 |

---

## 1. 认证

### POST `/api/auth/login` 🔓

登录获取 Token（公开接口）。

**Request Body:**
```json
{
  "username": "admin",
  "password": "password123"
}
```

**Response 200:**
```json
{
  "token": "a1b2c3d4...64位hex",
  "username": "admin"
}
```

**Response 401:**
```json
{ "error": "Invalid credentials" }
```

**认证逻辑**：优先匹配配置文件中 `admin_user`/`admin_password`，再匹配 Redis `CONF:AUTH` 中的运行时凭据。

---

### POST `/api/auth/logout`

登出，移除 Token。

**Response 200:**
```json
{ "ok": true }
```

---

## 1.1 运营域 API v1

> NC-053 起新增。该 namespace 使用 NC-051 的 `ACC:*` / `AACC:*` / `MPGRP:*` /
> `SUB:*` / `BILL:*` / `SUPPLY:*` / `STATION:*` 运营域模型，不替换旧
> `/api/accounts` 和旧 Web 管理接口。

### GET `/api/v1/auth/session`

返回当前进程内 Bearer token 的兼容 subject。

**Response 200:**
```json
{
  "username": "admin",
  "role": "admin",
  "account_id": "",
  "compat_admin": true
}
```

> 当前 HTTP token 仍是进程内 session。旧 admin 登录尚未迁移到 `ACC:*` 时，
> `account_id` 为空，`role=admin` 代表兼容管理员 subject。

### Admin Account APIs

| Method | Path | Redis | 说明 |
| --- | --- | --- | --- |
| `GET` | `/api/v1/admin/accounts` | `ACC:RECORD` | 列出运营域 Account |
| `POST` | `/api/v1/admin/accounts` | `ACC:RECORD` / `ACC:USERNAME` | 创建 Account |
| `GET` | `/api/v1/admin/accounts/{account_id}` | `ACC:RECORD` | 查询 Account |
| `PUT` | `/api/v1/admin/accounts/{account_id}` | `ACC:RECORD` | 更新 Account，不允许改 username |
| `DELETE` | `/api/v1/admin/accounts/{account_id}` | `ACC:RECORD` / `ACC:USERNAME` | 软删除并 tombstone username |
| `PUT` | `/api/v1/admin/accounts/{account_id}/group-grants` | `ACC:GROUP:{account_id}` | 授权 MountPointGroup |
| `POST` | `/api/v1/admin/accounts/{account_id}/balance-adjustments` | `ACC:BALANCE:LEDGER:{period}` / `ACC:RECORD` | 写余额调整事实并同步余额 |

创建 Account 示例：

```json
{
  "account_id": "acc_user_1",
  "username": "customer-a",
  "role": "user",
  "balance_cents": 10000,
  "concurrency_limit": 3
}
```

### Admin Group / MountPoint APIs

| Method | Path | Redis | 说明 |
| --- | --- | --- | --- |
| `GET` | `/api/v1/admin/mount-point-groups` | `MPGRP:RECORD` | 列出挂载点分组 |
| `POST` | `/api/v1/admin/mount-point-groups` | `MPGRP:RECORD` | 创建挂载点分组 |
| `PUT` | `/api/v1/admin/mount-point-groups/{group_id}/members` | `MPGRP:MEMBER:{group_id}` | 增加或更新分组成员 |
| `GET` | `/api/v1/admin/mount-points` | `MOUNT:RECORD` | 列出挂载点 |
| `PUT` | `/api/v1/admin/mount-points/{mountpoint}` | `MOUNT:RECORD` | 创建挂载点主记录 |

### Admin Operations Read APIs

| Method | Path | Redis | 说明 |
| --- | --- | --- | --- |
| `GET` | `/api/v1/admin/access-accounts` | `AACC:RECORD` | 全局只读 AccessAccount；管理员不代建 |
| `GET` | `/api/v1/admin/subscriptions` | `SUB:RECORD` | 列出订阅 |
| `POST` | `/api/v1/admin/subscriptions` | `SUB:RECORD` / `SUB:ACCOUNT:{account_id}` | 创建订阅 |
| `GET` | `/api/v1/admin/subscriptions/{subscription_id}` | `SUB:RECORD` | 查询订阅 |
| `PUT` | `/api/v1/admin/subscriptions/{subscription_id}` | `SUB:RECORD` / `SUB:ACCOUNT:{account_id}` | 更新订阅并同步 account index |
| `DELETE` | `/api/v1/admin/subscriptions/{subscription_id}` | `SUB:RECORD` / `SUB:ACCOUNT:{account_id}` | 软删除订阅并移除 account index |
| `GET` | `/api/v1/admin/redeem-codes` | `REDEEM:CODE` | 列出兑换码 |
| `POST` | `/api/v1/admin/redeem-codes` | `REDEEM:CODE` | 创建兑换码 |
| `GET` | `/api/v1/admin/redeem-codes/{code}` | `REDEEM:CODE` | 查询兑换码 |
| `POST` | `/api/v1/admin/redeem-codes/{code}/redeem` | `REDEEM:ACCOUNT:{account_id}` / `ACC:BALANCE:LEDGER:{period}` / `ACC:RECORD` | 兑换到指定 Account 并同步余额 |
| `GET` | `/api/v1/admin/stations` | `STATION:RECORD` | 列出历史站点 |
| `GET` | `/api/v1/admin/usage?period=yyyyMM` | `BILL:ENTRY:{period}` | 列出计费用量事实 |
| `GET` | `/api/v1/admin/data-push-configs` | `DATA:PUSH:CONFIG` | 列出数据推送配置 |
| `POST` | `/api/v1/admin/data-push-configs` | `DATA:PUSH:CONFIG` | 创建数据推送配置 |
| `GET` | `/api/v1/admin/data-push-configs/{config_id}` | `DATA:PUSH:CONFIG` | 查询数据推送配置 |
| `PUT` | `/api/v1/admin/data-push-configs/{config_id}` | `DATA:PUSH:CONFIG` | 更新数据推送配置 |
| `DELETE` | `/api/v1/admin/data-push-configs/{config_id}` | `DATA:PUSH:CONFIG` | 软删除数据推送配置 |
| `GET` | `/api/v1/admin/data-push-jobs?period=yyyyMM` | `DATA:PUSH:JOB:{period}` | 列出数据推送任务 |
| `GET` | `/api/v1/admin/data-push-usage?period=yyyyMM` | `DATA:PUSH:{period}` | 列出数据推送用量和扣费事实 |
| `GET` | `/api/v1/admin/supply-usage?period=yyyyMM` | `SUPPLY:USAGE:{period}` | 列出供应事实 |
| `GET` | `/api/v1/admin/supplier-settlements?period=yyyyMM&supplier_account_id=...` | `SUPPLY:EARNING:{account_id}:{period}` | 列出供应商结算批次；未传 supplier 时扫描供应商/admin 账号当期结算 |
| `POST` | `/api/v1/admin/supplier-settlements` | `SUPPLY:EARNING:{account_id}:{period}` / `SUPPLY:USAGE:{period}` | 创建待付款供应商结算并标记供应事实 settled |
| `GET` | `/api/v1/admin/supplier-settlements/{settlement_id}?period=yyyyMM&supplier_account_id=...` | `SUPPLY:EARNING:{account_id}:{period}` | 查询结算批次 |
| `POST` | `/api/v1/admin/supplier-settlements/{settlement_id}/payment` | `SUPPLY:EARNING:{account_id}:{period}` | 更新结算付款状态和付款凭据 |

### Self-Service APIs

`/api/v1/me/*` 允许 `role=user` 或 `role=admin`，`/api/v1/supplier/*`
允许 `role=supplier` 或 `role=admin`。owner Account 一律从 Bearer token
subject 推导，请求体中的 `owner_account_id` / `kind` 不能覆盖真实 owner/scope。

| Method | Path | Redis | 说明 |
| --- | --- | --- | --- |
| `GET` | `/api/v1/me/profile` | `ACC:RECORD` | 当前用户 Account 脱敏资料 |
| `GET` | `/api/v1/me/dashboard` | `ACC:*` / `AACC:*` | 当前用户余额、授权和接入账号摘要 |
| `GET` | `/api/v1/me/allowed-groups` | `ACC:GROUP:{account_id}` / `MPGRP:RECORD` | 当前用户已授权分组 |
| `GET` | `/api/v1/me/mount-points` | `MPGRP:MEMBER:*` / `MOUNT:RECORD` | 当前用户可见挂载点 |
| `GET` | `/api/v1/me/access-accounts` | `AACC:RECORD` | 当前用户 `user_client` 接入账号 |
| `POST` | `/api/v1/me/access-accounts` | `AACC:*` | 创建当前用户 `user_client` 接入账号 |
| `GET` | `/api/v1/me/access-accounts/{id}` | `AACC:RECORD` | 查询自己的接入账号 |
| `PUT` | `/api/v1/me/access-accounts/{id}` | `AACC:*` | 更新自己的接入账号状态、分组、并发等 |
| `PUT` | `/api/v1/me/access-accounts/{id}/password` | `AACC:*` | 更新自己的接入账号密码 |
| `DELETE` | `/api/v1/me/access-accounts/{id}` | `AACC:*` | 软删除自己的接入账号并 tombstone username |
| `GET` | `/api/v1/me/subscriptions` | `SUB:ACCOUNT:{account_id}` | 当前用户订阅权益 |
| `GET` | `/api/v1/me/usage?period=yyyyMM` | `BILL:ENTRY:{period}` | 当前用户计费用量事实 |
| `GET` | `/api/v1/me/data-push/configs` | `DATA:PUSH:CONFIG` | 当前用户可用数据推送配置 |
| `GET` | `/api/v1/me/data-push/jobs?period=yyyyMM` | `DATA:PUSH:JOB:{period}` | 当前用户数据推送任务 |
| `POST` | `/api/v1/me/data-push/jobs` | `DATA:PUSH:JOB:{period}` / `DATA:PUSH:{period}` / `ACC:BALANCE:LEDGER:{period}` / `ACC:RECORD` | 创建当前用户数据推送任务并扣费 |
| `GET` | `/api/v1/me/data-push?period=yyyyMM` | `DATA:PUSH:{period}` | 当前用户数据推送用量和扣费事实 |
| `POST` | `/api/v1/me/data-push` | `DATA:PUSH:{period}` / `ACC:BALANCE:LEDGER:{period}` / `ACC:RECORD` | 追加当前用户数据推送用量，按 `actual_debit_cents` 扣费 |
| `GET` | `/api/v1/me/redeem-redemptions` | `REDEEM:ACCOUNT:{account_id}` | 当前用户兑换记录 |
| `GET` | `/api/v1/supplier/profile` | `ACC:RECORD` | 当前供应商 Account 脱敏资料 |
| `GET` | `/api/v1/supplier/dashboard` | `ACC:*` / `SUPPLY:*` | 当前供应商供应摘要 |
| `GET` | `/api/v1/supplier/access-accounts` | `AACC:RECORD` | 当前供应商 `supplier_station` 接入账号 |
| `POST` | `/api/v1/supplier/access-accounts` | `AACC:*` | 创建当前供应商 `supplier_station` 接入账号 |
| `GET` | `/api/v1/supplier/access-accounts/{id}` | `AACC:RECORD` | 查询自己的供应接入账号 |
| `PUT` | `/api/v1/supplier/access-accounts/{id}` | `AACC:*` | 更新自己的供应接入账号 |
| `PUT` | `/api/v1/supplier/access-accounts/{id}/password` | `AACC:*` | 更新自己的供应接入账号密码 |
| `DELETE` | `/api/v1/supplier/access-accounts/{id}` | `AACC:*` | 软删除自己的供应接入账号 |
| `GET` | `/api/v1/supplier/stations` | `STATION:RECORD` | 当前供应商最近供应过的站点 |
| `GET` | `/api/v1/supplier/supply-usage?period=yyyyMM` | `SUPPLY:USAGE:{period}` | 当前供应商供应事实 |
| `GET` | `/api/v1/supplier/settlements?period=yyyyMM` | `SUPPLY:EARNING:{account_id}:{period}` | 当前供应商结算批次 |
| `GET` | `/api/v1/supplier/earnings?period=yyyyMM` | `SUPPLY:USAGE:{period}` / `SUPPLY:EARNING:{account_id}:{period}` | 当前供应商收益摘要 |

`POST /api/v1/me/data-push` 请求体至少包含 `usage_id`。服务端从 Bearer session
推导 `account_id`，会忽略请求体中的 `account_id`；`period` 缺省为当前 `yyyyMM`。
当 `actual_debit_cents` 大于 0 时，同步写入余额 ledger 并更新
`ACC:RECORD.balance_cents`。余额不足返回 `409`，不写入 `DATA:PUSH` 或 ledger。

`POST /api/v1/me/data-push/jobs` 请求体包含 `config_id`、`used_seconds` 和可选
`period`、`operator_note`。服务端按 `DATA:PUSH:CONFIG[config_id]` 的
`fixed_hourly_price_cents` 计算扣费，并覆盖 `account_id`、`usage_id`、`ledger_id`、
`balance_after_cents`、`stat_cost_cents` 和 `actual_debit_cents` 等客户端提交字段。
余额不足或配置禁用时返回错误，不写入 job、usage 或 ledger。

`POST /api/v1/admin/redeem-codes/{code}/redeem` 可通过 query 参数或 JSON body 传入
`account_id`。兑换成功会生成兑换记录和余额 ledger，增加 Account 余额；同一 Account 对
同一 code 只能兑换一次，禁用、过期或超过 `max_redemptions` 的兑换码返回 `409`。

`POST /api/v1/admin/supplier-settlements` 请求体至少包含 `supplier_account_id`，
`period` 缺省为 `current`。服务端会选取该供应商该账期所有未结算 `SUPPLY:USAGE`
记录，汇总 `used_seconds` 和 `earning_cents`，写入结算批次并把这些供应事实标记为
`settled`；结算批次默认 `status=pending_payment`。没有待结算用量时返回 `409`。

`POST /api/v1/admin/supplier-settlements/{settlement_id}/payment` 请求体包含
`supplier_account_id`、`period`、`status`，其中 `status` 可为 `paid`、
`payment_failed` 或 `cancelled`。可选字段 `payment_method`、`payment_ref`、
`payment_note` 会保存到结算批次；标记为 `paid` 时写 `paid_time`，已付款结算不能
再改为失败或取消。`settled` 历史状态在展示和汇总中按已付款兼容处理。

通用错误：

```json
{ "error": "message" }
```

`RepositoryStatus` 映射为：`Invalid -> 400`，`NotFound -> 404`，
`Conflict -> 409`，`RedisError -> 500`。

---

## 2. 账户管理

> Redis Key: `ACT:RECORD`（auth Redis 实例）

### GET `/api/accounts`

返回全部账户记录。

**Response 200:** `Record<string, AccountRecord>`

---

### GET `/api/accounts/active`

返回当前活跃（在线）的账户列表。

> Redis Key: 优先聚合 `ACT:SESSION:<account>`（auth Redis 实例），并兼容
> legacy `STR:ACTIVE` fallback。`ACT:ACTIVE` 是登录索引，不作为在线会话来源。

**Response 200:** `Record<string, AccountActive>`

---

### GET `/api/accounts/{account}`

获取单个账户详情。

**Response 200:** `AccountRecord`
**Response 404:** `{ "error": "Not found" }`

---

### POST `/api/accounts`

创建新账户。

**Request Body:**
```json
{
  "account": "user01",
  "password": "secret",
  "group": "default",
  "enabled": true
}
```

> `account` 字段为必填，作为 Hash field key。

**Response 201:**
```json
{ "ok": true, "account": "user01" }
```

**Response 409:**
```json
{ "error": "Account already exists" }
```

---

### PUT `/api/accounts/{account}`

更新账户（完整覆盖）。

**Request Body:** 完整的 `AccountRecord` JSON

**Response 200:** `{ "ok": true }`

---

### DELETE `/api/accounts/{account}`

删除账户。

**Response 200:** `{ "ok": true }`
**Response 404:** `{ "error": "Account not found" }`

---

## 3. 源表记录

> Redis Key: `MPT:RECORD`

### GET `/api/sources`

返回全部手动配置的源表记录。

**Response 200:** `Record<string, SourceRecord>`

---

### GET `/api/sources/{mountpoint}`

获取单个源记录。

**Response 200:** `SourceRecord`
**Response 404:** `{ "error": "Not found" }`

---

### POST `/api/sources`

创建源记录。

**Request Body:**
```json
{
  "mountpoint": "RTCM3_GPS",
  "identifier": "MyStation",
  "format": "RTCM 3.3",
  "format_details": "1005(1),1074(1),1084(1),1094(1),1124(1)",
  "carrier": "2",
  "nav_system": "GPS+GLO+GAL+BDS",
  "country": "CHN",
  "latitude": "30.0",
  "longitude": "114.0"
}
```

> `mountpoint` 字段为必填，作为 Hash field key。

**Response 201:**
```json
{ "ok": true, "mountpoint": "RTCM3_GPS" }
```

---

### PUT `/api/sources/{mountpoint}`

更新源记录（完整覆盖）。

**Response 200:** `{ "ok": true }`

---

### DELETE `/api/sources/{mountpoint}`

删除源记录。

**Response 200:** `{ "ok": true }`
**Response 404:** `{ "error": "Not found" }`

---

## 4. 基站状态（只读）

> Redis Key: `MPT:STAT`

### GET `/api/servers`

返回全部在线基站的实时状态。

**Response 200:** `Record<string, ServerState>`

**ServerState 字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `uid` | string | 唯一标识（node_id:mount） |
| `mountpoint` | string | 挂载点名称 |
| `node_id` | string | 所在节点 ID |
| `host` | string | 来源 IP |
| `port` | number | 来源端口 |
| `connect_time` | string | 连接时间戳 |
| `bytes_received` | number | 已接收字节数 |
| `messages_received` | number | 已接收消息数 |

---

### GET `/api/servers/{uid}`

获取单个基站状态。

---

## 5. 用户状态（只读）

> Redis Key: `USR:STAT`

### GET `/api/clients`

返回全部在线用户的实时状态。

**Response 200:** `Record<string, ClientState>`

---

### GET `/api/clients/{uid}`

获取单个用户状态。

---

## 6. 数据流状态（只读）

> Redis Key: `STR:STAT`

### GET `/api/streams`

返回全部数据流统计信息。

**Response 200:** `Record<string, StreamState>`

---

### GET `/api/streams/{uid}`

获取单个数据流统计。

---

## 7. 别名规则

> Redis Key: `ALIAS:RULE`

### GET `/api/aliases`

返回全部别名规则。

**Response 200:** `Record<string, AliasRule>`

---

### GET `/api/aliases/{uid}`

获取单个别名规则。

---

### POST `/api/aliases`

创建别名规则。

**Request Body:**
```json
{
  "uid": "alias_001",
  "alias_name": "VRS01",
  "source_name": "RTCM3_GPS",
  "enable": true,
  "visible": true
}
```

> Key 字段优先级：`uid` → `alias_name` → `alias_mpt` → `name`

**Response 201:**
```json
{ "ok": true, "alias": "alias_001" }
```

**Response 409:**
```json
{ "error": "Already exists" }
```

---

### PUT `/api/aliases/{uid}`

更新别名规则（完整覆盖）。

**Response 200:** `{ "ok": true }`

---

### DELETE `/api/aliases/{uid}`

删除别名规则。

**Response 200:** `{ "ok": true }`

---

## 8. 访问控制组

> Redis Key: `ACCESS:GROUP`

系统初始化时自动确保 `default` 组存在。

### GET `/api/access/groups`

返回全部访问控制组。

**Response 200:** `Record<string, AccessGroup>`

---

### GET `/api/access/groups/{uid}`

获取单个访问控制组。

---

### POST `/api/access/groups`

创建访问控制组。

**Request Body:**
```json
{
  "uid": "vip_group",
  "name": "VIP用户组",
  "description": "高级用户权限"
}
```

> Key 字段优先级：`uid` → `group_uid`

**Response 201:**
```json
{ "ok": true, "uid": "vip_group" }
```

---

### PUT `/api/access/groups/{uid}`

更新访问控制组。

---

### DELETE `/api/access/groups/{uid}`

删除访问控制组。

---

## 9. 访问控制项

> Redis Key: `ACCESS:ITEM:<group_uid>`（每个组独立 Hash）

### GET `/api/access/items/{group_uid}`

获取指定组内的全部访问项。

**Response 200:** `Record<string, AccessItem>`

---

### POST `/api/access/items/{group_uid}`

为指定组添加访问项。

**Request Body:**
```json
{
  "mountpoint": "RTCM3_GPS",
  "permission": "allow"
}
```

> `mountpoint`（或 `mount`）为必填，作为 Hash field key。

**Response 201:** `{ "ok": true }`

---

### PUT `/api/access/items/{group_uid}`

更新访问项。

**Request Body:** 需包含 `mountpoint`（或 `mount`）标识。

**Response 200:** `{ "ok": true }`

---

### DELETE `/api/access/items/{group_uid}`

删除访问项。

**Request Body:**
```json
{
  "mountpoint": "RTCM3_GPS"
}
```

> ⚠️ DELETE 通过 Body 传递目标项标识。

**Response 200:** `{ "ok": true }`
**Response 404:** `{ "error": "Item not found" }`

---

## 10. Pull 中继

> Redis Key: `PULL:RECORD`（配置）/ `PULL:STAT`（状态）

### GET `/api/relays/pull`

返回全部 Pull 中继配置。

**Response 200:** `Record<string, PullRecord>`

---

### GET `/api/relays/pull/status`

返回全部 Pull 中继运行状态。

**Response 200:** `Record<string, PullState>`

---

### GET `/api/relays/pull/{uid}`

获取单个 Pull 中继配置。

---

### POST `/api/relays/pull`

创建 Pull 中继任务。

**Request Body:**
```json
{
  "uid": "pull_001",
  "host": "rtk2go.com",
  "port": 2101,
  "mountpoint": "REMOTE_STN",
  "local_mountpoint": "LOCAL_STN",
  "username": "user",
  "password": "pass",
  "enabled": true
}
```

> `uid` 为必填。若未提供 `enabled` 则默认为 `true`。

**Response 201:**
```json
{ "ok": true, "uid": "pull_001" }
```

---

### PUT `/api/relays/pull/{uid}`

更新 Pull 中继配置。

> ⚠️ 同时删除对应 `PULL:STAT` 条目以强制重启任务。

**Response 200:** `{ "ok": true }`

---

### DELETE `/api/relays/pull/{uid}`

删除 Pull 中继任务。

> 同时删除对应 `PULL:STAT` 条目。

**Response 200:** `{ "ok": true }`

---

### POST `/api/relays/pull/start/{uid}`

启动指定 Pull 中继。

> 读取配置记录 → 设置 `enabled = true` → 写回

**Response 200:** `{ "ok": true, "uid": "pull_001" }`
**Response 404:** `{ "error": "Record not found" }`

---

### POST `/api/relays/pull/stop/{uid}`

停止指定 Pull 中继。

> 设置 `enabled = false`。**不**删除 state 条目（保留以触发 INACTIVE 广播）。

**Response 200:** `{ "ok": true, "uid": "pull_001" }`

---

## 11. Push 中继

> Redis Key: `PUSH:RECORD`（配置）/ `PUSH:STAT`（状态）

接口与 Pull 中继完全对称，路径前缀为 `/api/relays/push`。

### GET `/api/relays/push` — 全部 Push 配置
### GET `/api/relays/push/status` — 全部 Push 状态
### GET `/api/relays/push/{uid}` — 单个 Push 配置
### POST `/api/relays/push` — 创建 Push 中继
### PUT `/api/relays/push/{uid}` — 更新 Push 中继（同时删除状态记录）
### DELETE `/api/relays/push/{uid}` — 删除 Push 中继（同时删除状态记录）
### POST `/api/relays/push/start/{uid}` — 启动 Push
### POST `/api/relays/push/stop/{uid}` — 停止 Push

---

## 12. 集群节点（只读）

> Redis Key: `CASTER:NODE`

### GET `/api/nodes`

返回全部集群节点信息。

**Response 200:** `Record<string, CasterNode>`

---

### GET `/api/nodes/{uid}`

获取单个节点信息。

---

## 13. 挂载点订阅者

### GET `/api/mountpoints/subscribers`

查询各在线挂载点的订阅者数量。

> 遍历 `MPT:LIST` 所有在线挂载点 → 对每个执行 `HLEN MPT:SUB:<mpt>`

**Response 200:**
```json
{
  "RTCM3_GPS": 5,
  "VRS01": 12,
  "BASE02": 0
}
```

类型：`Record<string, number>`

---

## 14. 系统状态

### GET `/api/status`

获取系统运行状态。

**Response 200:**
```json
{
  "cpu_percent": 2.5,
  "memory_bytes": 52428800,
  "memory_mb": 50,
  "caster": {
    "node_id": "node_01",
    "is_master": true,
    "uptime": 86400,
    "connections": 150
  },
  "redis_caster_connected": true,
  "redis_auth_connected": true,
  "ntrip_port": 4202
}
```

---

### GET `/api/status/health` 🔓

健康检查（公开接口）。

**Response 200:**
```json
{ "status": "ok" }
```

---

## 15. 配置管理

> Redis Key: `CONF:SERVICE` / `CONF:CORE` / `CONF:AUTH`

### GET `/api/config`

获取全部配置项。

**Response 200:**
```json
{
  "service": { ... },
  "core": { ... },
  "auth": { ... }
}
```

> 仅返回已设置的配置节，未设置的忽略。

---

### GET `/api/config/{section}`

获取指定配置节。`section` 可选值：`service`、`core`、`auth`

> ⚠️ `auth` 节仅返回 `{ "admin_user": "<username>" }`，**永远不暴露密码**。

**Response 404:** `{ "error": "Unknown config section" }` 或 `{ "error": "Config not found" }`

---

### PUT `/api/config/{section}`

更新配置。

**普通 section (service / core):**

**Request Body:** 任意 JSON 对象，完整覆盖。

**auth section 特殊处理:**

**Request Body:**
```json
{
  "admin_user": "admin",
  "admin_password": "new_password",
  "old_password": "current_password"
}
```

> - **必须**包含 `old_password` 字段用于验证
> - 验证通过后自动移除 `old_password` 再保存
> - 优先校验 Redis 中的密码，回退到配置文件密码

**Response 200:** `{ "ok": true }`
**Response 400:** `{ "error": "需要输入当前密码" }`
**Response 403:** `{ "error": "当前密码错误" }`

---

## 16. 工具接口

### POST `/api/utils/sourcetable`

拉取远程 NTRIP Caster 的源表。

> ⚠️ **此接口使用同步阻塞 TCP 连接**，可能阻塞整个事件循环最多 5+ 秒。生产环境建议优先使用本地源表接口。

**Request Body:**
```json
{
  "host": "rtk2go.com",
  "port": 2101,
  "username": "",
  "password": "",
  "ntrip_version": "2.0"
}
```

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `host` | string | ✅ | - | 远程主机地址 |
| `port` | number | - | `2101` | 端口号 |
| `username` | string | - | `""` | Basic Auth 用户名 |
| `password` | string | - | `""` | Basic Auth 密码 |
| `ntrip_version` | string | - | `"2.0"` | `"1.0"` 或 `"2.0"` |

**Response 200:**
```json
{
  "ok": true,
  "mountpoints": [
    {
      "mountpoint": "RTCM3_GPS",
      "identifier": "MyStation",
      "format": "RTCM 3.3",
      "format_details": "1005(1),1074(1)",
      "country": "CHN",
      "latitude": "30.0",
      "longitude": "114.0"
    }
  ]
}
```

**Response 502:** `{ "error": "<详细错误信息>" }`（DNS/连接/发送失败）

---

### GET `/api/utils/sourcetable/local`

获取本地 Caster 当前的源表。

> 直接调用 `CASTER::Get_Source_Table_Text()`，不经过网络。

**Response 200:**
```json
{
  "ok": true,
  "mountpoints": [
    {
      "mountpoint": "RTCM3_GPS",
      "identifier": "MyStation",
      "format": "RTCM 3.3",
      "format_details": "1005(1),1074(1)",
      "country": "CHN",
      "latitude": "30.0",
      "longitude": "114.0"
    }
  ]
}
```

---

## 17. SSE 实时推送

### GET `/api/events/stream`

建立 Server-Sent Events 长连接，接收实时数据更新。

**认证方式**（二选一）：
- 查询参数：`?token=<token>`
- Header：`Authorization: Bearer <token>`

**可选查询参数**：
- `channels`：逗号分隔的频道列表，默认 `*`（订阅全部）

**示例**：`GET /api/events/stream?token=abc123&channels=servers,clients,streams`

**响应头**：
```
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
```

**推送格式**：
```
event: servers
data: {"node01:BASE01":{"uid":"node01:BASE01","mountpoint":"BASE01",...}}

event: clients
data: {"node01:user01":{"uid":"node01:user01","username":"user01",...}}
```

**更新频率**：每 2 秒检查一次当前有订阅者的频道，仅在数据变化时推送。

**可用频道**（16 个）：

| 频道名 | Redis Key | 数据类型 |
|--------|-----------|----------|
| `servers` | `MPT:STAT` | 基站在线状态 |
| `clients` | `USR:STAT` | 用户在线状态 |
| `streams` | `STR:STAT` | 数据流统计 |
| `nodes` | `CASTER:NODE` | 集群节点状态 |
| `accounts` | `ACT:RECORD` | 账户记录 |
| `sources` | `MPT:RECORD` | 源表记录 |
| `aliases` | `ALIAS:RULE` | 别名规则 |
| `access_groups` | `ACCESS:GROUP` | 访问控制组 |
| `pull_records` | `PULL:RECORD` | Pull 中继配置 |
| `pull_states` | `PULL:STAT` | Pull 中继状态 |
| `push_records` | `PUSH:RECORD` | Push 中继配置 |
| `push_states` | `PUSH:STAT` | Push 中继状态 |
| `account_actives` | `ACT:SESSION:*` + `STR:ACTIVE` fallback | 在线活跃账户 |

**连接行为**：
- 建立连接后立即推送所有订阅频道的当前快照
- 后续只轮询当前有订阅者的频道，并仅推送有变化的频道数据
- 服务端通过 `evhttp_connection_set_closecb` 检测客户端断开

---

## 通用错误响应

| HTTP 状态码 | 响应体 | 触发场景 |
|-------------|--------|----------|
| 400 | `{ "error": "Invalid JSON" }` | 请求体 JSON 解析失败 |
| 400 | `{ "error": "Missing <field>" }` | 缺少必填字段 |
| 401 | `{ "error": "Unauthorized", "message": "Invalid or missing token" }` | 未认证或 Token 无效 |
| 404 | `{ "error": "Not found" }` | 资源不存在 |
| 409 | `{ "error": "Already exists" }` | 创建时资源已存在 |
| 500 | `{ "error": "Internal Server Error" }` | 服务端内部错误 |
| 500 | `{ "error": "Redis error" }` | Redis 操作失败 |
| 502 | `{ "error": "<详细信息>" }` | 远程源表获取失败 |

## V3 运维接口 (2026-04)

| Method | Path | 说明 |
| --- | --- | --- |
| GET | /api/audit | 审计日志。Query: `limit` (默认 100, 上限 1000)、`cursor` (LRANGE 起始)、`actor`、`action` (子串)、`target`。返回 `{items, count, total, has_more, next_cursor}`。底层 Redis `LOG:AUDIT`，保留最近 50000 条 |
| GET | /api/logs/ring | 进程内存环形日志。Query: `n` (50~5000), `level` (trace/debug/info/warn/err/critical) |
| GET | /api/system/events | 聚合节点 `LOG:NODE:*` 事件。Query: `limit` (默认 100, 上限 500) |
| GET | /api/monitor/redis/history | Redis 状态时间序列 (60s 采样)。Query: `range=1h\|6h\|24h` |
| POST | /api/nodes/log-level/:id | 动态修改节点日志级别。Body `{level}`。`:id` 不为本节点 (可使用 `self`) 时返回 501 |

### 其他变动

- `GET /api/status` 新增 `node_id`、`log_level`、`sse_clients`、`sse_max_clients`。
- 所有 `POST/PUT/DELETE` 请求均会被写入审计日志 (`/api/auth/login` 除外)。Payload 中 `password / token / secret / admin_password` 字段会被脱敏为 `***`。
- SSE 订阅信道改为完全匹配 (不再做子串包含匹配)，SSE 连接默认上限 200，超过返回 429。

