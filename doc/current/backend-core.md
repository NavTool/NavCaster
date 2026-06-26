# Backend Core 当前实现说明

更新时间：2026-06-27
来源：NC-035 backend-core 文档审计、源码和 NC-017 至 NC-060 任务记录。

## 负责范围

```text
src/core      CASTER facade、Redis key、cluster/master、relay 调度、运行态和历史。
src/auth      NTRIP 账号鉴权、在线连接桶、匿名登录、AUTH:BROADCAST。
src/service   进程入口、配置加载、NTRIP listener、process queue、session 生命周期。
src/base      NTRIP/RTCM/NMEA/Base64/网络/系统资源基础工具。
proto         连接、配置、状态、监控和管理结构契约。
```

## 当前事实

- Redis key registry 在 `src/core/context/redis_keys.*`，覆盖 `CASTER:MASTER`、`CASTER:NODE`、`MPT:*`、`USR:*`、`PULL:*`、`PUSH:*`、`ACT:*`、`LOG:*`、`NODE:HISTORY:*`、`MONITOR:REDIS:HISTORY`、`STAT:DAILY:*` 和 Pub/Sub channel。
- Auth 写侧维护 `ACT:REC:<account>`、`ACT:UND:<name>`、`ACT:SESSION:<account>`，并使用 `HSETEX`、`HEXPIRE`、`HDEL` 和 `AUTH:BROADCAST`。
- NC-055 后 Auth 会优先查 `AACC:ACTIVE`；命中 AccessAccount 运行时索引时按 owner/account/group/mount/balance/kind 校验，缺失时回退旧 `ACT:ACTIVE` 兼容路径。
- NC-058 后 Auth 周期续期会对运行中的 AccessAccount 连接重读 `AACC:ACTIVE`，
  复验 access/owner 状态、AccessAccount 过期和 user_client 下一计费切片余额。
  不合规则通过既有 `AUTH:BROADCAST` 断连，断连后仍走 NC-055 的用量和 ledger
  finalization。
- NC-059 后 user_client 登录会命中 `SUB:ACCOUNT:<owner_account_id>` 的有效订阅并保存
  subscription snapshot；subscription 模式写 `BILL:ENTRY` 但不扣余额，周期重验发现
  subscription 过期/禁用/不覆盖 group 时通过 `AUTH:BROADCAST` 断连。supplier_station
  finalization 会按运行时价格快照写入 `SUPPLY:USAGE.earning_cents`。
- NC-060 后 AccountDomainRepository 支持订阅更新/删除和兑换码入账。订阅删除会移除
  `SUB:ACCOUNT:<owner_account_id>` 中的运行时索引，使 NC-059 的周期重验以
  `subscription_revoked` 断开连接；兑换码和余额调整都会写 `ACC:BALANCE:LEDGER:<yyyyMM>`、
  更新 `ACC:RECORD.balance_cents`，并刷新 `AACC:ACTIVE.balance_cents` 快照。
- `/api/accounts/active` 和 SSE `account_actives` 的展示来源是 `ACT:SESSION:*`，并兼容 legacy `STR:ACTIVE` fallback；`ACT:ACTIVE` 是登录索引，不是在线会话来源。
- 新 AccessAccount 运行时在线视图写入 `ONLINE:SESSION:<owner_account_id>`；断开时写 `BILL:ENTRY:<yyyyMM>`、必要的 `ACC:BALANCE:LEDGER:<yyyyMM>`，供应商站点写 `SUPPLY:USAGE:<yyyyMM>`、`STATION:RECORD` 和 `STATION:EVENT:<mountpoint>`。
- `MPGRP:*` 是新运营挂载点分组；NC-055 会同步同名 `ACCESS:GROUP` 和 `ACCESS:ITEM:<group_id>`，让旧 Caster AccessPolicy 能识别新分组成员。
- NTRIP listener 解析 `SOURCE`、`GET`、`POST`、Basic Auth、`Ntrip-Version`、chunked 相关字段。现役 session 在 `src/service/session`。
- `doc/archive/code-snapshots/session` 是旧源码快照，不参与当前构建。
- Master lease 使用 `CASTER:MASTER`：首次抢占走 `SET ... NX EX`，续租走 `SET ... IFEQ ... EX`；状态规划由 `src/core/context/services/master_lease_service.*` 承担。
- Relay 分发由 `src/core/context/services/relay_scheduler.*` 规划，master 通过 `NODE:<node_id>` 发布指令；运行态写入/同步 `PULL:STAT` 和 `PUSH:STAT`。
- Grid 当前只有 proto 枚举、配置开关和 `CASTER::*` stub 痕迹，不能视为已实现功能。

## 近期证据

| 范围 | 证据 |
| --- | --- |
| Auth/NTRIP session | NC-017 至 NC-023、NC-055 任务、QA、Review。 |
| Relay start/stop/data forwarding | NC-026、NC-027、NC-028 任务、QA、Review。 |
| Master lease / Cluster / Relay failover | NC-029、NC-030、NC-031、NC-033 任务、QA、Review。 |
| Ninja 构建默认口径 | NC-034 任务、QA、Review。 |
| Account/AccessAccount runtime | NC-051、NC-054、NC-055、NC-058、NC-059、NC-060 任务、QA、Review。 |

## 不要误用

- 不要把 `references/historical/redis-legacy.txt` 当当前 Redis schema。
- 不要把 `archive/code-snapshots/grid` 当已实现 Grid 功能。
- 不要把 `archive/code-snapshots/session` 当当前 session 源码。
- 不要把 `ACT:ACTIVE` 当在线会话展示表。
- 不要用旧 roadmap checkbox 判断当前完成状态。

## 相关文档

- `current/redis-schema.md`
- `current/workflow.md`
- `deployment/redis.md`
- `qa/qa-gates.md`
- `references/protocols/ntrip/`
