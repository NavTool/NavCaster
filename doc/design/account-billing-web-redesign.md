# 账户、计费、供应商与 Web 三角色重构设计

更新时间：2026-06-26
任务：NC-050 Account / Billing / Web Redesign Spec
状态：设计准入草案
依据：

```text
F:\Projects\NavCaster\_team\tasks\active\NC-050-account-billing-web-redesign-spec.md
F:\Projects\NavCaster\shared\requirements\2026-06-26-account-billing-web-redesign.md
F:\Projects\NavCaster\shared\references\sub2api-structure-design-notes-2026-06-26.md
F:\Projects\NavCaster\repo\doc\current\backend-core.md
F:\Projects\NavCaster\repo\doc\current\backend-http.md
F:\Projects\NavCaster\repo\doc\current\web-console.md
F:\Projects\NavCaster\repo\doc\current\redis-schema.md
F:\Projects\NavCaster\repo\doc\api\api-reference.md
F:\Projects\NavCaster\repo\doc\design\http-multi-entry-productization.md
```

## 设计结论

本轮采用“Redis 分阶段过渡 + 明确领域边界 + 后续任务逐步落地”的路线，不引入关系型数据库，不做旧数据迁移，也不做旧 API / 旧 Web 兼容。

核心结论：

```text
1. Web 登录主体统一命名为 Account，角色三选一：admin / user / supplier。
2. admin 是能力超集，但底层仍只保存 role=admin，不实现多角色数组。
3. 实际登录 Caster 的凭证统一命名为 AccessAccount，和 Web Account 分离。
4. 旧 ACT:RECORD / ACT:ACTIVE 视为旧接入账号兼容层；新模型使用 ACC:* 和 AACC:*，实现阶段通过兼容适配逐步切换 Auth 读取路径。
5. 旧 ACCESS:GROUP / ACCESS:ITEM 视为旧访问策略；新 MountPointGroup 使用 MPGRP:*，不继承 inside/outside/nearby 语义。
6. SourceRecord 继续表示 NTRIP source table 记录；StationRecord 是历史站点资产，使用 STATION:*，不直接复用 MPT:RECORD。
7. 余额、订阅、长连接计费、数据推送、供应商供应和收益都写只追加事实记录，主状态不能从日志反推。
8. 长连接按秒统计，tick 周期默认 60 秒；按量模式实际扣余额，订阅模式只统计费用不扣余额。
9. 余额不足、订阅过期、授权撤销、账号禁用、并发下调和管理员踢线都必须有断连事件来源和失败策略。
10. HTTP 登录本轮继续使用进程内 Bearer token，但 session 内必须保存 account_id、role 和状态快照；仍要求固定管理入口或稳定 sticky 管理入口，不支持普通 round-robin 写入口。
11. Web 使用三套 role scope 信息架构：admin / user / supplier；admin 可进入用户和供应商能力区，但业务权限仍由 role=admin 超集判断。
12. 本轮不实现真实支付、提现、自动续费或供应商真实结算，只预留状态和审计基础。
```

## 非目标

```text
- 不直接修改产品源码。
- 不创建 NC-051 至 NC-056 实现 worktree。
- 不落地真实支付、外部充值通道、自动续费、提现或供应商真实结算。
- 不迁移旧数据。
- 不兼容旧 API 和旧 Web 页面。
- 不照搬 Sub2API 的 LLM 模型路由、OAuth 上游账号、图片计费、平台 quota 或支付业务。
- 不把 HTTP token 改成跨节点分布式 session；该能力留给后续 HTTP 会话产品化任务。
```

## 术语表

| 术语 | 含义 |
| --- | --- |
| Account | Web 登录主体，角色三选一：admin / user / supplier。 |
| AccessAccount | 实际用于登录 Caster 的安全凭证，归属于一个 Account。 |
| User AccessAccount | 用户角色创建的接入账号，用于客户端消费挂载点数据。 |
| Supplier AccessAccount | 供应商角色创建的接入账号，用于基站主动接入和供应时长归属。 |
| MountPointGroup | 挂载点分组，承载授权、倍率和成员挂载点集合。 |
| MountPoint | Caster 挂载点，可属于多个 MountPointGroup。 |
| StationRecord | 历史站点/基站记录，按挂载点名沉淀，不归属供应商。 |
| Subscription | Account 对一个或多个 MountPointGroup 的订阅权益。 |
| BillingUsageEntry | 长连接计费事实记录。 |
| DataIngressConfig | 管理员配置的数据推送入口和计费策略。 |
| DataPushUsage | 用户使用数据推送能力产生的用量事实。 |
| SupplierSupplyUsage | 供应商基站接入产生的供应时长和收益事实。 |
| OnlineSession | 当前在线连接展示和断连控制视图。 |

## 角色模型和权限原则

Account 保存且仅保存一个 `role`：

| role | 含义 | 能力边界 |
| --- | --- | --- |
| admin | 管理员 | 管理全局账号、分组、挂载点、站点、余额、订阅、兑换码、在线连接、审计和供应收益；默认具备 user 和 supplier 能力。 |
| user | 用户 | 自助管理自己的 User AccessAccount，消费挂载点数据，使用数据推送并消耗自身额度，查看自己的用量和连接。 |
| supplier | 供应商 | 自助管理自己的 Supplier AccessAccount，主动接入基站，查看供应记录、在线基站、供应时长和收益。 |

权限原则：

```text
- user / supplier API 的 account_id 必须从登录 session 推导，不接受客户端传入任意 account_id。
- admin API 必须显式目标 ID，并写审计日志。
- admin 可以使用 user 和 supplier 能力，但仍以 role=admin 作为超集判断。
- AccessAccount 永远归属于一个 Account。
- AccessAccount 备注只对 owner Account 和 admin 审计查询可见；普通 owner 间不可见。
- 管理员不代用户或供应商创建、修改、禁用、删除 AccessAccount，只能全局查询、审计和踢下线。
- Caster 接入认证只使用 AccessAccount，不使用 Web token。
```

## 权限矩阵

| 资源 | admin | user | supplier |
| --- | --- | --- | --- |
| Account | 全局 CRUD、启停、并发、余额、订阅、授权 | 读写自身 profile / password | 读写自身 profile / password |
| AccessAccount | 全局查询、审计、踢线，不代建 | CRUD 自己的 user access account | CRUD 自己的 supplier access account |
| MountPointGroup | CRUD、成员、倍率、授权 | 只读被授权分组 | 只读被授权供应分组或接入分组 |
| MountPoint | CRUD、价格、归组 | 只读授权分组内挂载点 | 只读授权分组内挂载点 |
| StationRecord | 全局查询、编辑、关联历史 | 只读自己可见分组内站点状态 | 只读自己供应历史涉及的站点 |
| Balance | 调整任意 Account 余额 | 只读自身余额 | 只读自身余额和收益摘要 |
| Subscription | 管理任意 Account 订阅 | 只读自身订阅 | 只读自身订阅 |
| RedeemCode | 生成、禁用、查询、审计 | 兑换自身可用码 | 兑换自身可用码 |
| OnlineSession | 全局查询、按连接/账号/客户踢线 | 查询自身 AccessAccount 连接 | 查询自身 AccessAccount 和供应连接 |
| BillingUsageEntry | 全局查询和导出 | 查询自身消费记录 | 查询自身消费记录，若有 |
| DataIngressConfig | 管理配置 | 只读可用配置，发起数据推送 | 只读供应相关配置 |
| DataPushUsage | 全局查询 | 查询自身数据推送用量 | 无默认访问，除非 admin 超集 |
| SupplierSupplyUsage | 全局查询、统计、导出 | 无默认访问，除非 admin 超集 | 查询自身供应记录和收益 |
| AuditLog | 全局查询 | 查询自身敏感操作摘要，默认可后置 | 查询自身敏感操作摘要，默认可后置 |

越权负例必须进入 NC-056：

```text
- user 通过 URL 或 body 传入其他 account_id 查询 AccessAccount。
- supplier 查询其他供应商 supply usage。
- user 创建 AccessAccount 绑定未授权 MountPointGroup。
- supplier 使用 user access account 类型累计供应时长。
- admin 通过 customer access account API 代建普通用户凭证。
- 过期 subscription 覆盖连接继续收包。
```

## 领域模型

### Account

主状态。字段建议：

| 字段 | 说明 |
| --- | --- |
| account_id | 内部 ID，建议 `acc_<uuid>`。 |
| username | Web 登录名，全局唯一，软删除后可由设计决定是否复用；本轮建议不复用。 |
| role | `admin` / `user` / `supplier`。 |
| status | `active` / `disabled` / `deleted`。 |
| password_hash / password_algo / password_salt / password_iterations | Web 登录密码材料。 |
| balance_cents | 账户余额，整数分。 |
| credit_limit_cents | 可选授信额度，本轮默认 0。 |
| concurrency_limit | Account 总并发上限。 |
| allowed_group_count | 派生展示字段，不作为权限来源。 |
| create_time / update_time / delete_time | 生命周期时间。 |
| remark | 管理员备注。 |

约束：

```text
- role 必填且三选一。
- disabled / deleted 需要触发 Web session 失效和 Caster 连接断连。
- balance_cents 只能通过余额调整、兑换码、扣费事务变更。
```

### AccessAccount

实际 Caster 登录凭证。字段建议：

| 字段 | 说明 |
| --- | --- |
| access_account_id | 内部 ID，建议 `aacc_<uuid>`。 |
| owner_account_id | 所属 Account。 |
| username | Caster Basic Auth 用户名，全局唯一，删除后不可复用。 |
| kind | `user_client` / `supplier_station`。 |
| password_hash / password_algo / password_salt / password_iterations | Caster 登录密码材料。 |
| status | `active` / `disabled` / `deleted`。 |
| mount_point_group_id | 绑定且仅绑定一个 MountPointGroup。 |
| concurrency_limit | 接入账号并发上限，不能超过 owner concurrency_limit。 |
| expire_time | 可选凭证过期时间。 |
| ip_allowlist / ip_blocklist | 可选 IP 控制。 |
| private_remark | owner 可见备注。 |
| admin_remark | admin 审计备注。 |
| create_time / update_time / delete_time | 生命周期时间。 |

约束：

```text
- username 全局唯一，写入 tombstone，删除后不可复用。
- owner role=user 只能创建 kind=user_client。
- owner role=supplier 只能创建 kind=supplier_station。
- owner role=admin 可创建两类凭证，但归属仍为 admin 自身。
- AccessAccount 状态变化必须刷新登录索引并触发断连。
```

### MountPointGroup / MountPoint

MountPointGroup 是业务分组，不再继承旧 AccessGroup 的 inside/outside/nearby 语义。

MountPointGroup 字段：

| 字段 | 说明 |
| --- | --- |
| group_id | 内部 ID，建议 `mpg_<uuid>`。 |
| name | 分组名称，全局唯一。 |
| status | `active` / `disabled` / `deleted`。 |
| billing_multiplier | 计费倍率，允许 0，允许小于 1，不允许小于 0。 |
| description | 描述。 |
| create_time / update_time / delete_time | 生命周期时间。 |

MountPoint 字段：

| 字段 | 说明 |
| --- | --- |
| mountpoint | 挂载点名，主键。 |
| status | `active` / `disabled` / `deleted`。 |
| hourly_price_cents | 基础小时价格，允许 0。 |
| source_record_mount | 可选 SourceRecord 关联。 |
| create_time / update_time / delete_time | 生命周期时间。 |

关系：

```text
- mount_point_group_members(group_id, mountpoint)
- account_allowed_mount_point_groups(account_id, group_id)
- AccessAccount.mount_point_group_id 固定绑定一个 group。
```

### Subscription

订阅是 Account 对一个或多个 MountPointGroup 的权益实例。

| 字段 | 说明 |
| --- | --- |
| subscription_id | 内部 ID。 |
| account_id | 所属 Account。 |
| group_ids | 覆盖 MountPointGroup 列表。 |
| status | `active` / `expired` / `disabled` / `deleted`。 |
| start_time / expire_time | 有效期。 |
| granted_by | admin account_id 或 redeem code。 |
| remark | 备注。 |

规则：

```text
- 连接命中 active subscription 覆盖的 group 时不扣余额，但仍写统计费用。
- 订阅过期只断开订阅覆盖范围内连接，不断开同一 Account 的按量连接。
- disabled / expired 事件必须进入断连调度。
```

### Balance / RedeemCode

余额主状态在 Account 上，余额变更写 Ledger。

| 记录 | 说明 |
| --- | --- |
| BalanceLedgerEntry | 余额调整、扣费、兑换码、退款或修正的事实记录。 |
| RedeemCode | 可兑换余额、并发数、订阅等权益的码。 |

规则：

```text
- balance_cents 使用整数分。
- 展示金额到分。
- 计费计算中间值使用 decimal 字符串或定点整数；实现短期可用 double，但写入时必须明确四舍五入到分。
- RedeemCode 兑换必须幂等，记录 actor、account_id、code、benefit snapshot。
```

### StationRecord

站点是接入资产，按 mountpoint 沉淀。

| 字段 | 说明 |
| --- | --- |
| station_id | 内部 ID，建议 `st_<uuid>`。 |
| mountpoint | 挂载点名，全局唯一，作为自然键。 |
| display_name | 管理员可编辑名称。 |
| first_seen_time | 首次登录/推送时间。 |
| last_seen_time | 最近登录/推送时间。 |
| total_online_seconds | 累计在线时长。 |
| current_online | 在线态。 |
| last_access_account_id | 最近使用 AccessAccount。 |
| last_supplier_account_id | 最近供应商 Account，若有。 |
| source_record_snapshot | 最近源表快照。 |
| admin_note | 管理员备注。 |

StationEvent 只追加，记录每次登录、推送、断开和供应归属。

StationRecord 与 SourceRecord 的关系：

```text
- SourceRecord 仍负责 sourcetable 展示和 NTRIP 源表字段。
- StationRecord 负责历史站点资产和供应追溯。
- 登录或主动推送成功时，可以从 SourceRecord / ServerState 快照补充 StationRecord。
- 管理员编辑 StationRecord 不反写 SourceRecord，除非后续任务明确引入同步开关。
```

### 用量事实表

| 事实 | 主体 | 用途 |
| --- | --- | --- |
| BillingUsageEntry | Account + AccessAccount + session | 长连接统计和按量扣费。 |
| DataPushUsage | Account + DataIngressConfig | 用户数据推送计费和统计。 |
| SupplierSupplyUsage | Supplier Account + AccessAccount + StationRecord | 供应时长和收益统计。 |

共同要求：

```text
- 只追加。
- 保存价格、倍率、订阅命中、收益规则、秒数和金额快照。
- 有幂等键。
- 不承担主状态职责。
```

## Redis / 存储模型

本轮先沿用 Redis，按 key registry 明确新模型。关系型数据库留给后续支付、结算和大型报表扩展。

### 新 key 规划

| Key | Type | Field / Index | Value | 生命周期 | Owner |
| --- | --- | --- | --- | --- | --- |
| `ACC:RECORD` | HASH | account_id | Account JSON | 持久 | AccountRepository |
| `ACC:USERNAME` | HASH | username | account_id / tombstone JSON | 持久 | AccountRepository |
| `ACC:GROUP:<account_id>` | HASH | group_id | grant JSON | 持久 | AccountRepository |
| `ACC:BALANCE:LEDGER:<yyyyMM>` | HASH | ledger_id | BalanceLedgerEntry JSON | 持久 | BillingRepository |
| `AACC:RECORD` | HASH | access_account_id | AccessAccount JSON | 持久 | AccessAccountRepository |
| `AACC:USERNAME` | HASH | username | access_account_id / tombstone JSON | 持久 | AccessAccountRepository |
| `AACC:ACTIVE` | HASH | username | AccessAccountAuthIndex JSON | 持久 | AccessAccountRepository |
| `AACC:OWNER:<account_id>` | HASH | access_account_id | summary JSON | 持久 | AccessAccountRepository |
| `MPGRP:RECORD` | HASH | group_id | MountPointGroup JSON | 持久 | MountPointGroupRepository |
| `MPGRP:MEMBER:<group_id>` | HASH | mountpoint | member JSON | 持久 | MountPointGroupRepository |
| `MOUNT:RECORD` | HASH | mountpoint | MountPoint JSON | 持久 | MountPointRepository |
| `SUB:RECORD` | HASH | subscription_id | Subscription JSON | 持久 | SubscriptionRepository |
| `SUB:ACCOUNT:<account_id>` | HASH | subscription_id | summary JSON | 持久 | SubscriptionRepository |
| `REDEEM:CODE` | HASH | code_hash | RedeemCode JSON | 持久 | RedeemRepository |
| `ONLINE:SESSION:<account_id>` | HASH | connect_key | OnlineSession JSON | 在线 TTL | Auth/Billing |
| `BILL:ENTRY:<yyyyMM>` | HASH | billing_id | BillingUsageEntry JSON | 持久 | BillingRepository |
| `BILL:ACCOUNT:<account_id>:<yyyyMM>` | LIST | index | billing_id | 持久或定期归档 | BillingRepository |
| `BILL:IDEMPOTENT` | HASH | billing_id | fingerprint | 持久或按月归档 | BillingRepository |
| `DATA:INGRESS:CONFIG` | HASH | config_id | DataIngressConfig JSON | 持久 | DataIngressRepository |
| `DATA:PUSH:<yyyyMM>` | HASH | usage_id | DataPushUsage JSON | 持久 | DataPushRepository |
| `SUPPLY:USAGE:<yyyyMM>` | HASH | usage_id | SupplierSupplyUsage JSON | 持久 | SupplierRepository |
| `SUPPLY:ACCOUNT:<account_id>:<yyyyMM>` | LIST | index | usage_id | 持久或定期归档 | SupplierRepository |
| `SUPPLY:EARNING:<account_id>:<yyyyMM>` | HASH | period | earning summary JSON | 持久 | SupplierRepository |
| `STATION:RECORD` | HASH | mountpoint | StationRecord JSON | 持久 | StationRepository |
| `STATION:EVENT:<mountpoint>` | LIST | index | StationEvent JSON | 持久或归档 | StationRepository |

### 旧 key 兼容

| 旧 key | 本轮处理 |
| --- | --- |
| `ACT:RECORD` | 标记为旧接入账号主表。NC-051 引入 AACC 后停止作为新模型主状态。 |
| `ACT:ACTIVE` | 标记为旧 Auth 登录索引。NC-054 前可由 AACC:ACTIVE 镜像生成，Auth 改造后直接读 AACC:ACTIVE。 |
| `ACT:SESSION:<account>` | 旧在线展示桶。NC-054 新写 ONLINE:SESSION，并按需要镜像给旧 SSE。 |
| `ACCESS:GROUP` / `ACCESS:ITEM:*` | 旧访问策略。MPGRP 不直接复用该语义；是否迁移由 NC-051 明确。 |
| `MPT:RECORD` / `MPT:SOURCE` | SourceRecord，不作为 StationRecord 主表。 |
| `LOG:AUDIT` | 保留，扩展 action/target/payload 结构。 |

迁移原则：

```text
- 旧数据不迁移。
- 新写路径只写新 key；必要时为旧读路径做短期镜像。
- 每个镜像都必须在任务卡里写明删除条件。
- key registry 更新必须和 doc/current/redis-schema.md 后续同步任务配套。
```

## HTTP API 边界

### 登录

本轮继续使用进程内 Bearer token，不引入 JWT 或 Redis token store。

```text
POST /api/v1/auth/login
POST /api/v1/auth/logout
GET  /api/v1/auth/session
```

登录响应：

```json
{
  "token": "64_hex",
  "account_id": "acc_xxx",
  "username": "alice",
  "role": "user",
  "display_name": "Alice",
  "expires_at": 0
}
```

约束：

```text
- HTTP session map 保存 token -> account_id / role / username / status snapshot。
- Account disabled/deleted 后需要让 token 失效；短期可在每次受保护请求查 Account 状态。
- 多节点仍要求固定或 sticky 管理入口。
```

### User API

```text
GET    /api/v1/me/profile
PUT    /api/v1/me/profile
PUT    /api/v1/me/password
GET    /api/v1/me/dashboard
GET    /api/v1/me/allowed-groups
GET    /api/v1/me/mount-points
GET    /api/v1/me/access-accounts
POST   /api/v1/me/access-accounts
GET    /api/v1/me/access-accounts/:id
PUT    /api/v1/me/access-accounts/:id
PUT    /api/v1/me/access-accounts/:id/password
DELETE /api/v1/me/access-accounts/:id
GET    /api/v1/me/online-sessions
GET    /api/v1/me/usage
GET    /api/v1/me/data-push/configs
POST   /api/v1/me/data-push/jobs
GET    /api/v1/me/data-push/usage
```

User API 不接受 account_id。所有 owner 从 session 推导。

### Supplier API

```text
GET    /api/v1/supplier/profile
PUT    /api/v1/supplier/profile
GET    /api/v1/supplier/dashboard
GET    /api/v1/supplier/access-accounts
POST   /api/v1/supplier/access-accounts
PUT    /api/v1/supplier/access-accounts/:id
PUT    /api/v1/supplier/access-accounts/:id/password
DELETE /api/v1/supplier/access-accounts/:id
GET    /api/v1/supplier/stations
GET    /api/v1/supplier/online-stations
GET    /api/v1/supplier/supply-usage
GET    /api/v1/supplier/earnings
```

Supplier API 不接受 supplier_account_id。所有 owner 从 session 推导。

### Admin API

```text
GET    /api/v1/admin/accounts
POST   /api/v1/admin/accounts
GET    /api/v1/admin/accounts/:id
PUT    /api/v1/admin/accounts/:id
PUT    /api/v1/admin/accounts/:id/password-reset
PUT    /api/v1/admin/accounts/:id/status
PUT    /api/v1/admin/accounts/:id/concurrency
POST   /api/v1/admin/accounts/:id/balance-adjustments
GET    /api/v1/admin/accounts/:id/subscriptions
POST   /api/v1/admin/accounts/:id/subscriptions
PUT    /api/v1/admin/accounts/:id/group-grants
GET    /api/v1/admin/access-accounts
GET    /api/v1/admin/mount-point-groups
POST   /api/v1/admin/mount-point-groups
PUT    /api/v1/admin/mount-point-groups/:id
PUT    /api/v1/admin/mount-point-groups/:id/members
GET    /api/v1/admin/mount-points
PUT    /api/v1/admin/mount-points/:mountpoint
GET    /api/v1/admin/stations
PUT    /api/v1/admin/stations/:mountpoint
GET    /api/v1/admin/online-sessions
POST   /api/v1/admin/online-sessions/:connect_key/kick
POST   /api/v1/admin/access-accounts/:id/kick
POST   /api/v1/admin/accounts/:id/kick
GET    /api/v1/admin/usage
GET    /api/v1/admin/data-push/usage
GET    /api/v1/admin/supply-usage
GET    /api/v1/admin/supplier-earnings
GET    /api/v1/admin/redeem-codes
POST   /api/v1/admin/redeem-codes
PUT    /api/v1/admin/redeem-codes/:id/status
GET    /api/v1/admin/audit
```

管理员边界：

```text
- 可以创建 Account，但不代建 AccessAccount。
- 可以全局查询 AccessAccount 和在线连接。
- 可以踢线和调整 owner Account 状态、余额、订阅、授权、并发。
- 所有 POST/PUT/DELETE 写审计。
```

### Caster Access API

NTRIP SOURCE / GET / POST 继续使用 Basic Auth。认证对象从旧 AccountRecord 改为 AccessAccountAuthIndex。

认证快照至少包含：

```text
access_account_id
access_username
access_kind
owner_account_id
owner_role
owner_status
access_status
mount_point_group_id
account_concurrency_limit
access_concurrency_limit
balance_cents
subscription_snapshot
billing_mode
price_snapshot
```

## Caster 接入认证和连接生命周期

连接建立：

```text
1. 解析 SOURCE / GET / POST 和 Basic Auth。
2. 用 username 查 AACC:ACTIVE。
3. 校验 AccessAccount active、owner Account active、密码、IP 规则。
4. 校验目标 mountpoint 属于 AccessAccount.mount_point_group_id。
5. 校验 owner Account 对该 group 仍有授权。
6. 计算 billing mode：订阅命中优先，否则按量。
7. 按量模式要求余额至少覆盖一个 tick 的预计费用。
8. 校验 Account 总并发和 AccessAccount 并发。
9. 建立 OnlineSession，写 ONLINE:SESSION:<account_id>。
10. 若是 SOURCE / supplier_station，创建或更新 StationRecord，并开始 SupplierSupplyUsage 计时。
```

连接期间：

```text
- 每 60 秒或连接结束时生成 BillingUsageEntry。
- 按量连接实际扣余额。
- 订阅连接只统计费用。
- supplier_station 连接同步生成 SupplierSupplyUsage 周期事实。
- source 登录/推送状态刷新 StationRecord。
- 续期 OnlineSession TTL。
```

连接断开：

```text
- 写最终 BillingUsageEntry。
- 写最终 SupplierSupplyUsage。
- 更新 StationRecord total_online_seconds / current_online / last_seen_time。
- 删除 OnlineSession。
- 写断连原因。
```

断连原因枚举建议：

```text
client_closed
server_closed
account_disabled
access_account_disabled
balance_insufficient
subscription_expired
group_revoked
account_concurrency_reduced
access_concurrency_reduced
admin_kick
billing_persist_failed
unknown
```

## 计费、订阅和余额扣费状态机

费用公式：

```text
stat_cost_cents = round_to_cent(used_seconds / 3600 * mount_hourly_price_cents * group_multiplier)
actual_debit_cents = stat_cost_cents when billing_mode=payg else 0
```

计费 tick：

```text
tick_seconds = min(60, now - last_billed_at)
billing_id = session_id + ":" + billing_period_start
fingerprint = owner_account_id + access_account_id + mountpoint + group_id + tick_seconds + price_snapshot + billing_mode
```

幂等规则：

```text
- BILL:IDEMPOTENT[billing_id] 不存在：写入 fingerprint，应用扣费和日志。
- 已存在且 fingerprint 相同：返回成功，不重复扣费。
- 已存在且 fingerprint 不同：拒绝并报警，保守断连。
```

余额不足：

```text
- 连接建立前余额不足：拒绝登录。
- tick 扣费后余额小于下一 tick 预计费用：断开同一 Account 全部按量连接。
- 不断开订阅覆盖连接。
```

订阅过期：

```text
- subscription expire_time 到期后，断开命中该 subscription 覆盖 group 的连接。
- 同一 Account 的按量连接不受影响。
- 后台扫描周期建议 30 秒，写断连事件并通过 AUTH/CASTER broadcast 执行。
```

持久化失败：

```text
- BillingUsageEntry 写入失败：短暂重试。
- 重试超过阈值：断开连接，reason=billing_persist_failed。
- 不允许静默继续提供未记账服务。
```

## 数据推送用量

本轮将数据推送作为用户能力。设计结论：

```text
- DataIngressConfig 由管理员配置。
- user/admin 可按授权范围使用数据推送。
- 数据推送不复用长连接 tick，使用独立 DataPushUsage 事实记录。
- 计费单位本轮定为推送连接在线秒数，后续可扩展为次数或流量。
- 默认费用公式复用挂载点基础价格和 group_multiplier；如果 DataIngressConfig 指定 fixed_hourly_price_cents，则优先使用配置价格。
- DataPushUsage 归属 Account，不归属 AccessAccount，除非调用方显式绑定某个 AccessAccount。
```

DataPushUsage 快照字段：

```text
usage_id
account_id
config_id
target_mountpoint
group_id
start_time
end_time
used_seconds
price_snapshot
stat_cost_cents
actual_debit_cents
billing_mode
request_id
result
```

## 供应商供应时长和收益

供应归属：

```text
- 只有 kind=supplier_station 的 AccessAccount 能产生 SupplierSupplyUsage。
- owner Account role=supplier 或 admin 时可以累计供应。
- 基站成功 SOURCE / POST 登录后开始计时。
- StationRecord 只作为站点资产，不作为供应商资产。
```

收益规则本轮采用可配置快照，默认先按固定小时收益：

```text
earning_cents = used_seconds / 3600 * supplier_hourly_rate_cents
```

后续可扩展为：

```text
- 挂载点价格分成。
- 分组倍率分成。
- 分等级供应商费率。
- 质量评分折扣。
```

SupplierSupplyUsage 字段：

```text
usage_id
supplier_account_id
access_account_id
station_id
mountpoint
session_id
start_time
end_time
used_seconds
earning_rule_snapshot
earning_cents
status = pending / settled / adjusted / void
```

本轮不实现真实结算；`settled` 仅为后续结算任务预留，不在 NC-051 至 NC-056 中自动出款。

## StationRecord 站点历史沉淀

StationRecord 触发条件：

```text
- supplier_station SOURCE / POST 登录成功。
- 其他主动推送到 Caster 平台的基站数据登录成功。
- 管理员手工创建或编辑站点。
```

更新规则：

```text
- 按 mountpoint 查找 StationRecord。
- 不存在则创建。
- 登录成功写 StationEvent(type=login)。
- 连接结束写 StationEvent(type=disconnect)，累计 online seconds。
- 每次事件保存 supplier_account_id、access_account_id、session_id、node_id、source snapshot。
```

查询能力：

```text
- admin 查询所有历史站点。
- supplier 查询自己供应过的站点和在线基站。
- user 只读自己可见分组内站点状态，默认不看供应商收益信息。
```

## 并发控制和断连触发器

并发口径：

```text
Account total concurrency = owner 下所有 AccessAccount 当前连接总数。
AccessAccount concurrency = 单个 AccessAccount 当前连接数。
AccessAccount.concurrency_limit <= Account.concurrency_limit。
```

断连触发器：

| 事件 | 来源 | 目标范围 | 策略 |
| --- | --- | --- | --- |
| Account disabled/deleted | Admin API | Account 全部连接和 Web token | 立即失效 token，广播踢线。 |
| AccessAccount disabled/deleted | Owner API 或 Admin 状态变更 | 指定 AccessAccount 连接 | 刷新登录索引，广播踢线。 |
| Balance insufficient | Billing tick | Account 全部按量连接 | 保留订阅连接，断开 payg 连接。 |
| Subscription expired | Expiry scanner | subscription 覆盖 group 的连接 | 只断开订阅命中连接。 |
| Group grant revoked | Admin API | 绑定撤销 group 的 AccessAccount 连接 | 标记相关 AccessAccount 不可用或拒绝目标 group，广播踢线。 |
| Concurrency reduced | Admin API | 超额连接 | 优先断开最早连接，记录 reason。 |
| Admin kick | Admin API | connect_key / access_account / account | 直接广播踢线。 |

失败策略：

```text
- 广播失败时记录审计和系统事件。
- 下次 tick 或续期检查必须再次执行断连判断。
- Auth 登录链路 fail-closed。
```

## 审计日志和对账

管理员写操作审计字段：

```text
audit_id
timestamp
actor_account_id
actor_role
action
target_type
target_id
source_ip
node_id
before_summary
after_summary
reason
result
error_message
```

必须审计：

```text
- Account 创建、禁用、启用、密码重置、并发调整。
- 余额调整。
- 订阅创建、修改、禁用。
- 分组授权变更。
- MountPoint / MountPointGroup 变更。
- StationRecord 编辑。
- RedeemCode 生成、禁用、兑换。
- 在线连接踢下线。
- DataIngressConfig 变更。
```

对账：

```text
- BalanceLedgerEntry 是余额变化事实。
- BillingUsageEntry 是长连接使用事实。
- DataPushUsage 是数据推送使用事实。
- SupplierSupplyUsage 是供应收益事实。
- Account.balance_cents 是主状态，不能从 usage log 实时反推。
- 每个扣费事实需要关联 ledger_id。
```

## Web 三角色信息架构

### 路由策略

保持 HashRouter。新增 route meta：

```ts
type RoleScope = 'admin' | 'user' | 'supplier';
type AllowedRole = 'admin' | 'user' | 'supplier';
```

规则：

```text
- /login 公开。
- /admin/* 允许 role=admin。
- /supplier/* 允许 role=supplier 或 role=admin。
- /dashboard、/access-accounts、/usage、/groups、/profile、/data-push 允许 role=user 或 role=admin。
- admin 登录后默认进入 /admin/dashboard，同时可从菜单进入 user/supplier scope。
- user 登录默认进入 /dashboard。
- supplier 登录默认进入 /supplier/dashboard。
```

### 用户页面

```text
Dashboard
AccessAccounts
Usage
Groups
MountPoints
OnlineSessions
DataPush
Profile
```

### 供应商页面

```text
SupplierDashboard
SupplierAccessAccounts
StationRecords
OnlineStations
SupplyUsage
Earnings
SupplierProfile
```

### 管理员页面

```text
AdminDashboard
Customers
Suppliers
AccountDetail
BalanceAdjustments
Subscriptions
GroupGrants
GlobalAccessAccounts
OnlineSessions
MountPoints
MountPointGroups
Stations
SupplierEarnings
DataIngressConfigs
DataPushUsage
Usage
RedeemCodes
AuditLog
SystemMonitor
Settings
```

API 模块拆分：

```text
web/src/api/admin/*
web/src/api/me/*
web/src/api/supplier/*
web/src/api/auth.ts
web/src/api/types.ts
```

## 运维、监控和后续扩展点

本轮预留：

```text
- 支付订单：BalanceLedgerEntry 增加 source=payment_order。
- 自动续费：Subscription 增加 renewal_policy_id。
- 供应商结算：SupplierSupplyUsage.status 和 earning summary 可进入结算批次。
- 套餐运营：Subscription 增加 plan_id，MountPointGroup 可被套餐引用。
- 运行监控：Billing queue backlog、failed billing count、disconnect reason stats。
- 细粒度权限：Account 增加 permission policy 引用，但本轮不实现。
- 分布式 HTTP session：AuthSessionService 可替换 token store，但本轮保持进程内。
```

## 实施拆分

### NC-051-user-access-account-domain-model

```text
负责人：backend-core
分支：feature/NC-051-user-access-account-domain-model
worktree：F:\Projects\NavCaster\worktrees\backend-core\NC-051-user-access-account-domain-model
来源分支：team-dev
依赖：NC-050 QA_PASSED / APPROVED，team-dev 基线通过。
```

范围：

```text
- 新增 Account / AccessAccount / MountPointGroup / Subscription / StationRecord / usage 领域模型。
- 新增 Redis key registry 和 Repository 草案。
- 明确 ACT:* / ACCESS:* 兼容 adapter。
- 不接入 Caster 登录链路。
- 不实现 HTTP API 和 Web。
```

最低验证：

```text
schema_smoke 构建和执行。
相关 repository 单元或 schema smoke 覆盖。
API contract 不应变化，若变化必须记录原因。
```

### NC-052-http-admin-user-billing-apis

```text
负责人：backend-http
分支：feature/NC-052-http-admin-user-billing-apis
worktree：F:\Projects\NavCaster\worktrees\backend-http\NC-052-http-admin-user-billing-apis
依赖：NC-051。
```

范围：

```text
- 管理员 Account、余额、订阅、授权、并发、挂载点、站点、收益、兑换码和审计 API。
- 扩展 AuthSessionService 保存 account_id / role。
- 不实现用户/供应商自助 API。
- 不实现 Caster enforcement。
```

最低验证：

```text
schema_smoke 构建和执行。
CasterService 或 HTTP 目标构建。
关键 admin API curl。
审计日志脱敏检查。
```

### NC-053-http-customer-supplier-self-service-apis

```text
负责人：backend-http
分支：feature/NC-053-http-customer-supplier-self-service-apis
worktree：F:\Projects\NavCaster\worktrees\backend-http\NC-053-http-customer-supplier-self-service-apis
依赖：NC-051 / NC-052。
```

范围：

```text
- /api/v1/me/* 用户自助 API。
- /api/v1/supplier/* 供应商自助 API。
- subject 从 session 推导，补越权负例。
- 不实现 Web。
- 不实现 Caster enforcement。
```

最低验证：

```text
schema_smoke 构建和执行。
自助 API 权限负例 curl。
API contract check。
```

### NC-054-caster-auth-billing-enforcement

```text
负责人：backend-core
分支：feature/NC-054-caster-auth-billing-enforcement
worktree：F:\Projects\NavCaster\worktrees\backend-core\NC-054-caster-auth-billing-enforcement
依赖：NC-051，部分依赖 NC-052/NC-053 API 状态变更事件。
```

范围：

```text
- Auth 读取 AACC:ACTIVE。
- 接入认证校验 Account / AccessAccount / group / subscription / balance / concurrency。
- OnlineSession 写入。
- tick 计费、BillingUsageEntry、BalanceLedgerEntry。
- SupplierSupplyUsage 和 StationRecord 写入。
- 余额不足、订阅过期、授权撤销、禁用、并发下调断连。
```

最低验证：

```text
schema_smoke 构建和执行。
CasterService 构建。
Auth/NTRIP 连接矩阵。
余额不足断连、订阅过期断连、供应时长、站点沉淀 e2e 或模拟验证。
```

### NC-055-web-three-role-operations-ui

```text
负责人：frontend
分支：feature/NC-055-web-three-role-operations-ui
worktree：F:\Projects\NavCaster\worktrees\frontend\NC-055-web-three-role-operations-ui
依赖：NC-052 / NC-053 API shape 稳定。
```

范围：

```text
- 角色化路由和菜单。
- admin/user/supplier 首页和核心页面。
- AccessAccount 自助管理。
- 站点、供应、用量、余额、订阅、兑换码和审计相关页面。
- API 类型同步。
```

最低验证：

```text
npm run build。
受影响页面手工验证或截图。
如 lint 基线仍有债务，记录未运行原因或局部替代检查。
```

### NC-056-billing-usage-regression-and-hardening

```text
负责人：qa / reviewer
分支：test/NC-056-billing-usage-regression-and-hardening
worktree：F:\Projects\NavCaster\worktrees\qa\NC-056-billing-usage-regression-and-hardening
依赖：NC-051 至 NC-055 均 DEV_DONE、QA_PASSED、APPROVED 并合入 team-dev。
```

范围：

```text
- 反向检查 NC-050 需求覆盖矩阵。
- 权限越权测试。
- 计费精度和幂等测试。
- 余额/订阅/授权/并发断连测试。
- 数据推送用量测试。
- 供应时长和收益测试。
- StationRecord 历史沉淀测试。
- Web production build。
```

最低验证：

```text
Ninja Release configure。
schema_smoke 构建和执行。
CasterService 构建。
API contract check。
Web build。
关键 e2e smoke。
审计和敏感信息扫描。
```

## QA / Reviewer 准入矩阵

| 风险 | 设计要求 | 实现验证 |
| --- | --- | --- |
| 角色越权 | 三角色权限矩阵和 API namespace | user/supplier/admin 负例 curl + Web 路由检查 |
| AccessAccount 混淆 | Account 与 AccessAccount 独立模型 | 账号名唯一、删除不可复用、owner scope |
| 计费重复扣费 | billing_id + fingerprint | tick 重放测试 |
| 未记账服务 | billing persist fail-closed | 持久化失败模拟或保守断连路径检查 |
| 订阅错误断连 | subscription 覆盖 group 快照 | 订阅连接断开、按量连接保留 |
| 供应归属错误 | supplier_station kind 和 owner role 校验 | 供应商 AccessAccount 登录累计供应 |
| 站点资产混淆 | StationRecord 不归属供应商 | 同一站点多供应商历史可追溯 |
| Web 只隐藏菜单 | 后端 API subject 校验 | 直接调用越权 API 返回 403 |
| 旧 key 回归 | ACT/ACCESS/MPT 兼容边界 | 旧页面或新页面读写路径明确 |

## 待确认问题和风险

| 问题 | 本设计默认结论 | 后续处理 |
| --- | --- | --- |
| 是否引入关系型数据库 | 本轮不引入，继续 Redis 分阶段过渡 | 支付/结算/报表扩展时重评 |
| 旧 ACT:RECORD 怎么办 | 作为旧 AccessAccount 兼容层 | NC-051/NC-054 明确镜像和删除条件 |
| 旧 ACCESS:GROUP 怎么办 | 不承载新 MountPointGroup | NC-051 决定迁移或并存 |
| 数据推送计费单位 | 独立 DataPushUsage，默认按在线秒数 | NC-051/NC-054 细化 |
| 供应商收益规则 | 默认固定小时收益快照 | NC-052 暴露 admin 配置或常量 |
| 金额精度 | 主状态整数分，快照记录计算输入 | 实现可短期 double 但写入按分舍入 |
| tick 在哪里执行 | NC-054 在 Core/Auth session 生命周期内执行 | 后续可拆 billing service |
| HTTP token 是否跨节点 | 本轮不跨节点 | 继续遵守固定/sticky 管理入口 |
| StationRecord 是否反写 SourceRecord | 默认不反写 | 后续可加同步开关 |
| 是否需要 NC-051A | 默认不需要；若基线或存储脚手架过大再拆 | 总控按 NC-051 复杂度决定 |
