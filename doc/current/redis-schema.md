# NavCaster Redis Schema V2

生成时间：2026-06-26

本文档定义目标 Redis 数据模型。当前代码中已有部分 key 与本文档不完全一致，后续迭代以本文档为目标逐步迁移。

部署要求：当前 schema 依赖 hash field TTL 与条件 SET，生产 Redis 最低版本为
Redis Open Source 8.4.0+。部署和命令校验见 `deployment/redis.md`。

## 设计原则

- Redis 分四类数据：运行时状态、持久配置、账号鉴权、历史监控。
- 运行时状态必须有 TTL 或明确清理策略。
- 持久配置必须有 schema、默认值和迁移策略。
- 所有 key、field、value schema 统一登记，不在业务代码里散落硬编码。
- value 默认使用 proto JSON。少量历史日志可使用普通 JSON，但字段必须文档化。
- Pub/Sub 频道和数据存储 key 分开命名和管理。

## 命名约定

- `DOMAIN:KIND`：全局 hash/string/list。
- `DOMAIN:KIND:<id>`：按实体分桶。
- `NODE:<node_id>`：节点私有控制频道。
- `MPT:<mount>`、`USR:<user>`：二进制实时数据频道，保留现状。
- 历史数据使用分桶 key，避免单个大 hash。

## 数据分类

### 运行时状态

这类数据描述当前连接、节点、任务运行状态。应自动过期。

| Key | Type | Field | Value | 生命周期 | 写入者 | 读取者 |
| --- | --- | --- | --- | --- | --- | --- |
| `CASTER:MASTER` | STRING | - | node_id | `EX` 主节点续期 | ClusterService | ClusterService/HTTP |
| `CASTER:NODE` | HASH | node_id | `CasterNode` proto JSON | `HSETEX` | ClusterService | HTTP/SSE/RelayScheduler |
| `MPT:LIST` | HASH | mount | timestamp/source info | `HSETEX` | CasterService | SourceTable/HTTP/Core |
| `USR:LIST` | HASH | user | timestamp/basic info | `HSETEX` | CasterService | Core/HTTP |
| `MPT:STAT` | HASH | connect_key | `ServerState` proto JSON | `HSETEX` | CasterService | HTTP/SSE |
| `USR:STAT` | HASH | connect_key | `ClientState` proto JSON | `HSETEX` | CasterService | HTTP/SSE |
| `STR:STAT` | HASH | connect_key | `StreamState` proto JSON | `HSETEX` | CasterService | HTTP/SSE/Stats |
| `PULL:STAT` | HASH | task_uid | `PullState` proto JSON | `HSETEX` | Relay session | HTTP/SSE/RelayScheduler |
| `PUSH:STAT` | HASH | task_uid | `PushState` proto JSON | `HSETEX` | Relay session | HTTP/SSE/RelayScheduler |
| `MPT:REC:<mount>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService | CasterService |
| `USR:REC:<user>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService/AuthService | CasterService/AuthService |
| `MPT:SUB:<mount>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService | HTTP/Core |
| `USR:SUB:<user>` | HASH | connect_key | timestamp | `HSETEX` + `HEXPIRE` | CasterService | Core |
| `MPT:GEO` | GEO/ZSET | member=mount | lon/lat | 同在线状态清理 | CasterService | nearest |
| `USR:GEO` | GEO/ZSET | member=connect_key | lon/lat | 同在线状态清理 | CasterService | monitoring |

### 持久配置

这类数据由管理台配置，默认不过期。

| Key | Type | Field | Value | 生命周期 | 写入者 | 读取者 |
| --- | --- | --- | --- | --- | --- | --- |
| `MPT:RECORD` | HASH | mount | `SourceRecord` proto JSON | 持久 | HTTP/SourceRepository | SourceTable/Core |
| `MPT:SOURCE` | HASH | mount | `SourceRecord` proto JSON | `HSETEX` | CasterService 自动解码 | SourceTable/Core |
| `ALIAS:RULE` | HASH | uid | `AliasRule` proto JSON | 持久 | HTTP/AliasRepository | SourceTable/Core |
| `ACCESS:GROUP` | HASH | group_uid | `AccessGroup` proto JSON | 持久 | HTTP/AccessRepository | AccessPolicy |
| `ACCESS:ITEM:<group_uid>` | HASH | mount | `AccessItem` proto JSON | 持久 | HTTP/AccessRepository | AccessPolicy |
| `PULL:RECORD` | HASH | task_uid | `PullRecord` proto JSON | 持久 | HTTP/RelayRepository | RelayScheduler |
| `PUSH:RECORD` | HASH | task_uid | `PushRecord` proto JSON | 持久 | HTTP/RelayRepository | RelayScheduler |
| `CONF:SERVICE` | STRING or HASH | section | service config JSON/YAML | 持久 | HTTP/ConfigService | App/HTTP |
| `CONF:CORE` | STRING or HASH | section | core config JSON/YAML | 持久 | HTTP/ConfigService | Core |
| `CONF:AUTH` | STRING or HASH | section | auth config JSON/YAML | 持久 | HTTP/ConfigService | Auth/HTTP |

建议后续把 `CONF:*` 统一成 STRING JSON，避免当前文档与实现中 HASH/STRING 混用。

### 账号鉴权

目标语义：

| Key | Type | Field | Value | 生命周期 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `ACT:RECORD` | HASH | account | `AccountRecord` proto JSON | 持久 | 账号主表，Web CRUD 的唯一主数据 |
| `ACT:ACTIVE` | HASH | account | `AccountAuthIndex` 或兼容 `AccountRecord` JSON | 持久或按账号到期 TTL | NTRIP 登录快速校验索引 |
| `ACT:REC:<account>` | HASH | connect_key | timestamp/session JSON | 在线 TTL | 已登录实名账号连接 |
| `ACT:UND:<name>` | HASH | connect_key | timestamp/session JSON | 在线 TTL | 匿名登录连接 |
| `ACT:UNNAMED` | HASH | name | timestamp | 可选 TTL/清理 | 匿名账号痕迹 |
| `ACT:SESSION:<account>` | HASH | connect_key | `AccountActiveSession` JSON | 在线 TTL | NC-008A 起由 Auth 写侧维护，作为活跃账号 API 目标来源 |

本轮迭代定稿语义：

- `ACT:RECORD` 是账号主表，HTTP 账号 CRUD 只把它作为持久主数据。
- `ACT:ACTIVE` 是登录索引，只保存“当前允许登录”的账号派生视图；Auth 登录只读这个 key。
- `ACT:ACTIVE` 不表示在线会话，禁用、冻结、过期或删除账号时必须清理对应 field。
- `ACT:REC:<account>` 是实名账号在线连接桶，用于连接数限制和踢下线广播。
- `ACT:UND:<name>` 是匿名登录在线连接桶。
- `ACT:UNNAMED` 是匿名账号痕迹表，仅在匿名模式注册临时名称。
- `ACT:SESSION:<account>` 是实名账号展示会话桶；登录通过连接数限制后写入，续期时刷新，
  登出、拒绝登录或连接数踢线时删除对应 `connect_key`。
- Auth `Online_Protection=true` 表示已在线连接优先，实名连接数超过上限时拒绝新连接；
  `false` 表示允许新连接挤掉最早的旧实名连接。
- `STR:ACTIVE` 是旧版在线账号展示表，当前 `/api/accounts/active` 仍兼容读取它；后续读侧由 `ACT:SESSION:<account>` 聚合替换。

`ACT:SESSION:<account>` 当前 JSON 字段：

```json
{
  "uid": "<connect_key>",
  "connect_key": "<connect_key>",
  "account": "<account>",
  "anonymous": false,
  "auth_type": "client|server|source|unknown",
  "online_time": 1710000000,
  "update_time": 1710000005,
  "addr": "",
  "port": "",
  "group_uid": "default"
}
```

第一阶段 `addr`、`port` 为空字符串，因为 Auth 登录记录入口暂未接收远端地址。

当前实现状态：

- HTTP 账号管理以 `ACT:RECORD` 作为账号主表，并同步维护 `ACT:ACTIVE` 登录索引。
- NTRIP 鉴权读 `ACT:ACTIVE`；禁用、冻结、过期或删除账号时 HTTP 写侧会清理对应登录索引。
- 活跃账号 API/SSE 已切换为聚合 `ACT:SESSION:*`，并兼容读取 `STR:ACTIVE` fallback。
- Auth 写侧维护 `ACT:SESSION:<account>`；NC-016 至 NC-023 已覆盖读侧、真实 NTRIP 写入、续期、连接数、匿名、广播踢线和禁用账号矩阵。
- `STR:ACTIVE` 当前仅作为 legacy fallback；当前源码未发现新的写入路径。

NC-051 运营域分阶段新增：

```text
NC-051 在 Core 层新增 AccountDomainRepository，用于账户/接入账号/挂载点分组/站点/订阅/用量事实的
新 Redis 边界。该切片不接入 Auth 登录链路，不替换现有 /api/accounts，也不迁移旧 ACT:* 数据。
```

| Key | Type | Field/Index | Value | 生命周期 | 当前写入者 |
| --- | --- | --- | --- | --- | --- |
| `ACC:RECORD` | HASH | account_id | Account JSON | 持久 | AccountDomainRepository |
| `ACC:USERNAME` | HASH | username | account_id 索引或 tombstone JSON | 持久 | AccountDomainRepository |
| `ACC:GROUP:<account_id>` | HASH | group_id | group grant JSON | 持久 | AccountDomainRepository |
| `ACC:BALANCE:LEDGER:<yyyyMM>` | HASH | ledger_id | BalanceLedgerEntry JSON | 持久 | AccountDomainRepository |
| `AACC:RECORD` | HASH | access_account_id | AccessAccount JSON | 持久 | AccountDomainRepository |
| `AACC:USERNAME` | HASH | username | access_account_id 索引或 tombstone JSON | 持久 | AccountDomainRepository |
| `AACC:ACTIVE` | HASH | username | AccessAccountAuthIndex JSON | 持久 | AccountDomainRepository，NC-054 后 Auth 读取 |
| `AACC:OWNER:<account_id>` | HASH | access_account_id | owner summary JSON | 持久 | AccountDomainRepository |
| `MPGRP:RECORD` | HASH | group_id | MountPointGroup JSON | 持久 | AccountDomainRepository |
| `MPGRP:MEMBER:<group_id>` | HASH | mountpoint | member JSON | 持久 | AccountDomainRepository |
| `MOUNT:RECORD` | HASH | mountpoint | MountPoint JSON | 持久 | AccountDomainRepository |
| `SUB:RECORD` | HASH | subscription_id | Subscription JSON | 持久 | AccountDomainRepository |
| `SUB:ACCOUNT:<account_id>` | HASH | subscription_id | Subscription JSON summary | 持久 | AccountDomainRepository |
| `REDEEM:CODE` | HASH | code | RedeemCode JSON | 持久 | AccountDomainRepository |
| `REDEEM:ACCOUNT:<account_id>` | HASH | redemption_id | RedeemRedemption JSON | 持久 | AccountDomainRepository |
| `BILL:ENTRY:<yyyyMM>` | HASH | billing_id | BillingUsageEntry JSON | 持久 | AccountDomainRepository |
| `BILL:ACCOUNT:<account_id>:<yyyyMM>` | LIST | billing_id | billing_id | 持久或后续归档 | AccountDomainRepository |
| `BILL:IDEMPOTENT` | HASH | billing_id | fingerprint JSON | 持久或后续归档 | AccountDomainRepository |
| `DATA:PUSH:CONFIG` | HASH | config_id | DataPushConfig JSON | 持久 | AccountDomainRepository |
| `DATA:PUSH:JOB:<yyyyMM>` | HASH | job_id | DataPushJob JSON | 持久 | AccountDomainRepository |
| `DATA:PUSH:<yyyyMM>` | HASH | usage_id | DataPushUsage JSON | 持久 | AccountDomainRepository |
| `SUPPLY:USAGE:<yyyyMM>` | HASH | usage_id | SupplierSupplyUsage JSON | 持久 | AccountDomainRepository |
| `SUPPLY:ACCOUNT:<account_id>:<yyyyMM>` | LIST | usage_id | usage_id | 持久或后续归档 | AccountDomainRepository |
| `SUPPLY:EARNING:<account_id>:<yyyyMM>` | HASH | settlement_id | SupplierSettlement JSON | 持久 | AccountDomainRepository |
| `STATION:RECORD` | HASH | mountpoint | StationRecord JSON | 持久 | AccountDomainRepository |
| `STATION:EVENT:<mountpoint>` | LIST | event JSON | StationEvent JSON | 持久或后续归档 | AccountDomainRepository |

NC-051 约束：

- `role` 仅允许 `admin`、`user`、`supplier`；管理员超集只保存 `role=admin`，不使用多角色数组。
- `AccessAccount.kind` 仅允许 `user_client`、`supplier_station`。
- `user` owner 只能创建 `user_client`，`supplier` owner 只能创建 `supplier_station`，`admin` 可为自身创建两类。
- `AccessAccount.username` 全局唯一，删除后在 `AACC:USERNAME` 保留 tombstone，不可复用。
- `Account.username` 本轮同样在 `ACC:USERNAME` 保留 tombstone，不可复用。
- `MPGRP:*` 是新 MountPointGroup 语义，不复用 `ACCESS:GROUP` 的 inside/outside/nearby 策略。
- `STATION:*` 是站点历史资产，不等同于 `MPT:RECORD` 或 `MPT:SOURCE`。
- usage、ledger、supply、station event 均为只追加事实；主状态不能从这些日志反推。
- `BILL:IDEMPOTENT` 使用 `billing_id + fingerprint` 防止 tick 重放重复扣费。

NC-055 运行时接入状态：

```text
Auth/NTRIP 运行时已优先读取 AACC:ACTIVE，并保留 ACT:ACTIVE fallback。
MPGRP 创建和成员添加会同步同名 ACCESS:GROUP / ACCESS:ITEM:<group_id>，用于让旧 Caster AccessPolicy 识别新运营分组。
AccessAccount 登录成功后继续写旧 ACT:REC / ACT:SESSION 兼容桶，同时写新 ONLINE:SESSION:<owner_account_id>。
连接断开时写 BILL:ENTRY、BILL:IDEMPOTENT、BILL:ACCOUNT；按量扣费写 ACC:BALANCE:LEDGER，并同步更新 ACC:RECORD.balance_cents 与 AACC:ACTIVE.balance_cents 快照。
supplier_station 断开时写 SUPPLY:USAGE、SUPPLY:ACCOUNT、STATION:RECORD 和 STATION:EVENT:<mountpoint>。
```

NC-058 运行中连接重验：

```text
Auth 周期续期会对 access_runtime_enabled 的实名连接重读 AACC:ACTIVE[access_username]。
重验 access_status、owner_status、expire_time、access_kind、mount_point_group_id 和 user_client 下一计费切片余额。
不合规则通过 AUTH:BROADCAST 断连，并复用断开 finalization 写 BILL:ENTRY / ledger / ONLINE:SESSION 清理。
```

NC-059 订阅和供应收益：

```text
user_client 登录时读取 SUB:ACCOUNT:<owner_account_id>，选择覆盖 mount_point_group_id 且有效的 subscription。
命中后 ONLINE:SESSION / BILL:ENTRY 写 subscription_id 和 subscription_snapshot，billing_mode=subscription。
subscription 模式保留 stat_cost_cents，但 actual_debit_cents=0，不写 ACC:BALANCE:LEDGER。
Auth 周期续期会重读 SUB:ACCOUNT:<owner_account_id>[subscription_id]，过期断连 reason=subscription_expired，禁用/不覆盖 group 断连 reason=subscription_revoked。
supplier_station 断开时 SUPPLY:USAGE:<yyyyMM> 写 earning_cents 和 earning_rule_snapshot。
```

NC-061 / NC-063 供应商结算和付款状态：

```text
管理员创建结算时读取 SUPPLY:USAGE:<yyyyMM> 中指定供应商的 pending 供应事实。
结算成功写 SUPPLY:EARNING:<account_id>:<yyyyMM>[settlement_id]，字段包含 usage_ids、
usage_count、total_supply_seconds、total_earning_cents、status、create_time、update_time。
被纳入结算的 SUPPLY:USAGE 记录会更新 status=settled、settlement_id、settlement_time。
结算批次默认 status=pending_payment；管理员可更新为 paid、payment_failed 或 cancelled。
付款更新可写 payment_method、payment_ref、payment_note、paid_time、payment_failed_time、
payment_update_time。历史 status=settled 按 paid 兼容处理。
供应商自助收益摘要以 SUPPLY:USAGE.status 统计 pending_earning_cents，并从
SUPPLY:EARNING 拆分 pending_payment_cents、paid_earning_cents、failed_payment_cents
和 settlement_count；settled_earning_cents 保留为 paid + pending_payment 的兼容字段。
```

NC-060 订阅运营和兑换入账：

```text
管理员可通过 /api/v1/admin/subscriptions 创建、更新和删除订阅。
update 会同步 SUB:RECORD 和 SUB:ACCOUNT:<account_id>；跨 account_id 迁移时会删除旧 account index。
delete 会把 SUB:RECORD 标记为 deleted，并删除 SUB:ACCOUNT:<account_id>[subscription_id]。
运行时周期重验读不到 SUB:ACCOUNT 索引时按 subscription_revoked 断连。

管理员可通过 /api/v1/admin/redeem-codes 创建兑换码，并把兑换码兑换到指定 account。
兑换成功写 REDEEM:ACCOUNT:<account_id>[redemption_id] 和 ACC:BALANCE:LEDGER:<yyyyMM>[ledger_id]，
同步增加 ACC:RECORD.balance_cents，刷新 AACC:ACTIVE owner 余额快照，并递增 REDEEM:CODE.redeemed_count。
同一 account 对同一 code 只能兑换一次；禁用、过期、超过 max_redemptions 或余额 ledger 冲突会拒绝写入。
```

NC-057 数据推送用量：

```text
用户数据推送事实写 DATA:PUSH:<yyyyMM>，field 为 usage_id。
account_id 由 HTTP session 推导，不能由请求体覆盖。
actual_debit_cents > 0 时写 ACC:BALANCE:LEDGER:<yyyyMM>，同步更新 ACC:RECORD.balance_cents。
余额不足返回 Conflict，不写 DATA:PUSH 或 ledger。
```

NC-062 数据推送任务：

```text
管理员维护 DATA:PUSH:CONFIG[config_id]，配置包含 name、target_mountpoint、
fixed_hourly_price_cents、status 和可选 group_id/description。
用户通过 /api/v1/me/data-push/jobs 创建任务时，服务端从 session 覆盖 account_id，
按 config.fixed_hourly_price_cents 和 used_seconds 计算扣费，写
DATA:PUSH:JOB:<yyyyMM>[job_id]、DATA:PUSH:<yyyyMM>[usage_id]、
ACC:BALANCE:LEDGER:<yyyyMM>[ledger_id] 并同步 ACC:RECORD.balance_cents。
用户不能覆盖 usage_id、ledger_id、balance_after_cents、stat_cost_cents 或
actual_debit_cents。余额不足时不写 job、usage 或 ledger。
```

NC-064 DataPush relay execution bridge：

```text
DATA:PUSH:CONFIG.execution_mode 默认为 ledger_only；relay_push 模式额外要求
source_mountpoint、relay_target_host，并保存 relay_target_port、relay_target_mountpoint、
relay_target_account、relay_target_password、relay_push_type。
用户创建 relay_push 任务时，除 DATA:PUSH:JOB / DATA:PUSH / ledger 外，还写
PUSH:RECORD[data_push:<job_id>]。该记录 managed_by=data_push_job，enabled=true，
login_mpt=source_mountpoint，target_ip/target_port/target_mpt/target_account/
target_password 来自 relay_target_* 字段。
DATA:PUSH:JOB:<yyyyMM>[job_id] 记录 execution_mode、relay_uid、relay_record_key、
relay_status_key 和 relay_push_record 脱敏快照；任务初始 status=queued。
HTTP 查询任务时读取 PUSH:STAT[relay_uid] 补充 relay_status/relay_state，不把运行态
直接回写 DATA:PUSH:JOB。
```

NC-065 DataPush 任务控制：

```text
cancel relay_push 任务会把 DATA:PUSH:JOB:<yyyyMM>[job_id].status 更新为 cancelled，
写 cancel_time/control_time，并把 PUSH:RECORD[relay_uid].enabled=false，同时删除
PUSH:STAT[relay_uid]。
retry relay_push 任务会重新启用或重建受管 PUSH:RECORD[relay_uid]，删除旧 PUSH:STAT，
并把任务 status 更新为 queued，写 retry_time/control_time。
mark_failed 写 status=failed、failed_time/control_time，并禁用受管 PUSH:RECORD。
mark_completed 写 status=completed、complete_time/control_time，并清理 PUSH:STAT。
```

`ONLINE:SESSION:<account_id>` 当前 JSON 字段：

```json
{
  "connect_key": "<connect_key>",
  "account_id": "<owner_account_id>",
  "owner_account_id": "<owner_account_id>",
  "access_account_id": "<access_account_id>",
  "access_username": "<access_username>",
  "kind": "user_client|supplier_station",
  "mountpoint": "<mountpoint>",
  "group_id": "<group_id>",
  "billing_mode": "payg|subscription",
  "auth_type": "client|server|source|unknown",
  "start_time": 1710000000,
  "update_time": 1710000005,
  "addr": "127.0.0.1",
  "port": 2101,
  "user_agent": "NTRIP ...",
  "ntrip_version": "Ntrip/2.0"
}
```

`BILL:ENTRY:<yyyyMM>` 当前 JSON 字段：

```json
{
  "billing_id": "<connect_key>:<start_time>:<end_time>",
  "fingerprint": "<owner>|<access>|<mount>|<group>|<seconds>|<mode>|<cost>",
  "account_id": "<owner_account_id>",
  "access_account_id": "<access_account_id>",
  "access_username": "<access_username>",
  "mountpoint": "<mountpoint>",
  "group_id": "<group_id>",
  "connect_key": "<connect_key>",
  "start_time": 1710000000,
  "end_time": 1710000060,
  "used_seconds": 60,
  "billing_mode": "payg|subscription",
  "subscription_id": "<subscription_id when subscription>",
  "subscription_snapshot": {},
  "stat_cost_cents": 100,
  "actual_debit_cents": 100,
  "disconnect_reason": "client_closed"
}
```

`SUPPLY:USAGE:<yyyyMM>` 当前 JSON 字段：

```json
{
  "usage_id": "supply:<billing_id>",
  "supplier_account_id": "<owner_account_id>",
  "access_account_id": "<access_account_id>",
  "station_id": "st_<mountpoint>",
  "mountpoint": "<mountpoint>",
  "session_id": "<connect_key>",
  "start_time": 1710000000,
  "end_time": 1710000060,
  "used_seconds": 60,
  "earning_rule_snapshot": "runtime_price_snapshot:hourly_price_cents=...",
  "earning_cents": 100,
  "status": "pending"
}
```

V2 建议：

- `ACT:RECORD` 是唯一账号主表。
- `ACT:ACTIVE` 是由 `ACT:RECORD` 派生的可登录索引。
- `ACT:SESSION:<account>` 是展示用在线会话；`ACT:REC:<account>` 只作为连接数控制桶。
- `STR:ACTIVE` 标记为 legacy，仅作为 `ACT:SESSION:*` 聚合读的 fallback。

密码字段建议：

| 字段 | 说明 |
| --- | --- |
| `password_hash` | 新密码哈希，当前实现为 PBKDF2-SHA256 的 hex digest |
| `password_algo` | 当前支持 `pbkdf2-sha256`；未知算法登录 fail closed |
| `password_salt` | PBKDF2 salt |
| `password_iterations` | PBKDF2 迭代次数，默认 `100000` |
| `password` | legacy 明文字段，仅迁移期读取，不再写入 |

迁移规则：

- HTTP 新建或修改账号时，如果收到 legacy `password`，写入前自动转换为 `password_hash/password_algo/password_salt/password_iterations`，并从 `ACT:RECORD` 与 `ACT:ACTIVE` 派生视图中移除明文 `password`。
- HTTP 修改账号时，如果请求未携带新密码或仅携带空 `password`，沿用当前 Redis 记录中的密码材料，避免普通资料更新清空密码。
- Auth 登录优先校验 `password_hash`；仅当没有 hash 且存在 legacy `password` 时才走明文兼容。
- 同时存在 hash 和明文时 hash 优先，hash 算法未知、salt 缺失或迭代次数非法时拒绝登录。

### 历史和监控

| Key | Type | Field/Index | Value | 生命周期 |
| --- | --- | --- | --- | --- |
| `LOG:MPT:<mount>` | HASH | `<connect_time>_<connect_key>` | connection history JSON | 持久，后续可加清理 |
| `LOG:USR:<user>` | HASH | `<connect_time>_<connect_key>` | connection history JSON | 持久，后续可加清理 |
| `LOG:NODE:<node_id>` | HASH | `<ts>_<event>` | node event JSON | 持久或定期清理 |
| `LOG:AUDIT` | LIST | index | `AuditEntry` JSON | `LTRIM` 保留最近 N 条 |
| `LOG:AUDIT:SEQ` | STRING | - | sequence | 持久 |
| `NODE:HISTORY:<node_id>` | LIST | index | node metric JSON | `LTRIM` RAW |
| `NODE:HISTORY:<node_id>:1M` | LIST | index | 1min aggregate JSON | `LTRIM` |
| `NODE:HISTORY:<node_id>:5M` | LIST | index | 5min aggregate JSON | `LTRIM` |
| `MONITOR:REDIS:HISTORY` | LIST | index | Redis metric JSON | `LTRIM` |

后续如果历史查询成为核心能力，建议迁到关系型数据库或时序库；当前作为管理台监控数据，Redis list/hash 可接受。

## Pub/Sub 频道

| Channel | Payload | 发布者 | 订阅者 | 说明 |
| --- | --- | --- | --- | --- |
| `MPT:<mount>` | binary RTCM/raw | server/pull session | client/push/near session | 实时数据通道 |
| `USR:<user>` | binary NMEA/raw | client session | 需要 rover 上行数据的组件 | 用户上行通道 |
| `CASTER:BROADCAST` | `BroadcastMsg` proto JSON | Core/HTTP/Auth | all nodes | 踢下线、状态、relay 操作 |
| `CASTER:CONF` | string category | HTTP config | all nodes | 配置变更通知 |
| `NODE:<node_id>` | `BroadcastMsg` proto JSON | master | target node | relay 任务指令 |
| `AUTH:BROADCAST` | auth broadcast JSON | AuthService | AuthService | 账号连接限制、踢出 |

## Value Schema 约束

配置和状态类 value 优先使用 proto JSON：

- `SourceRecord`
- `AliasRule`
- `AccessGroup`
- `AccessItem`
- `PullRecord`
- `PushRecord`
- `ServerState`
- `ClientState`
- `StreamState`
- `CasterNode`
- `PullState`
- `PushState`
- `AccountRecord`
- `AccountActive`

存储 helper 必须：

- 忽略未知字段。
- 保留 proto field name。
- 补齐默认值。
- 在写入前做必填校验。
- 对 legacy 字段做兼容读取。

## 必填字段建议

### AccountRecord

- `account`
- `state`
- `active`
- `group_uid`
- `connection_limit`
- `type`

写入时补：

- `uid = account`
- `create_time`
- `update_time`
- 默认 `group_uid = default`

### SourceRecord

- `mountpoint`
- `source_group_uid`
- `record_type`
- `decode_type`
- `display_type`

写入时补：

- `uid = mountpoint`
- `create_time`
- `update_time`

### AliasRule

- `uid`
- `alias_name`
- `source_name`
- `enable`
- `visible`

### AccessGroup

- `uid`
- `group_name`
- 六个 allow 开关。

### AccessItem

- `mount_point_name`
- `allow_visible`
- `allow_access`
- `allow_nearby`

### PullRecord/PushRecord

- `uid`
- `login_mpt`
- `type`
- `target_ip`
- `target_port`
- `target_mpt`
- `enabled`

## 迁移策略

### Step 1：只读盘点

- 增加 Redis schema 文档和 key registry。
- 不改变现有读写行为。

### Step 2：账号双写（已实现）

- HTTP 写 `ACT:RECORD` 后同步 `ACT:ACTIVE`。
- Auth 继续读 `ACT:ACTIVE`。
- 活跃会话由 Auth 写入 `ACT:SESSION:*`，读侧仍兼容 `STR:ACTIVE` fallback。

### Step 3：账号读路径统一（已实现读侧聚合，AuthService 统一读侧仍待后续收口）

- `/api/accounts/active` 和 SSE `account_actives` 统一读取 `ACT:SESSION:*` 聚合，并合并
  legacy `STR:ACTIVE` fallback；冲突时 `ACT:SESSION:*` 优先。
- AuthService 通过 AccountRepository 读取账号。
- `ACT:ACTIVE` 变成缓存索引，可由 `ACT:RECORD` 重建。

### Step 4：废弃 legacy

- 管理台不再读 `STR:ACTIVE`。
- 清理或迁移旧 `ACT:ACCOUNT`、`STR:ACTIVE`。

## 当前高风险点

- `ACT:ACTIVE` 是由 `ACT:RECORD` 派生的登录索引，后续仍需要重建/迁移工具保证存量环境可恢复。
- `STR:ACTIVE` 仍保留为 legacy fallback，后续需要在确认无存量依赖后清理。
- 旧账号可能仍含 legacy 明文密码字段；新写入应使用 hash 材料并剥离明文输出。
- HTTP CRUD 对多个配置表直接 `body.dump()`，缺少 schema 校验。
- `MPT:GEO`、`USR:GEO` 清理依赖间接逻辑，需确认 stale member 清理。
- 大量 `HGETALL` 用于 SSE 和统计，规模上来后要按订阅频道和分页优化。

## 验收标准

- Redis key 有单一 registry。
- 每个 key 有 owner、TTL、value schema。
- Web 创建账号后 NTRIP 登录行为可预测。
- Redis 中账号密码不再明文新增。
- 所有持久配置写入都有校验和默认值。
- smoke test 覆盖账号创建、登录、禁用、连接数限制。
