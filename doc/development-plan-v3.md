# NavCaster 第三期迭代方案 (V3)

> 编写时间：2026-04
> 范围：在 V2（HTTP 线程化、稳定 Node ID、三层节点历史、Master 主从控制、集群字段修复）基础上，完成"业务可观测性 + 节点运维能力 + 前端纵深 + Proto 收口"四大目标。
> 目标使用者：Sonnet 4.6 编码代理；每个任务单元设计为 **30~90 分钟**可独立完成与验收的最小工作粒度。

## 0. 总体目标

| 维度 | V2 现状 | V3 目标 |
|------|---------|---------|
| 后端可观测 | Redis hash + LOG bucket，按需轮询 | + 审计日志 / + 环形日志缓冲 / + Redis 监控时间序列 |
| 节点运维 | 仅查看节点状态 | + 节点配置热更新 / + 远程日志级别 / + 集群操作审计 |
| 业务统计 | 后端有 `/api/stats/overview` | + 用户排行 / + 挂载点排行 / + 时间趋势图 |
| Proto | core/* 完备；monitor/* 多数 10 LOC | monitor/* 全字段化；新增 NodeConfig / AuditEntry 等 |
| 前端 | 详情页基本只展示概要 | 选项卡式深度页 / 审计页 / 系统设置完整表单 |

## 1. 主线分组

- **A. Proto 收口**（A1~A4，独立、零阻塞）
- **B. 后端业务能力**（B1~B7，依赖 A）
- **C. SSE/缓存优化**（C1~C3，独立）
- **D. 前端深度页 & 新增页**（D1~D8，依赖 B/A）
- **E. 体验/质量**（E1~E5，主要为前端）

依赖示意：
```
A* ──► B* ──► D*
        └─► C*
                 └─► E*
```

---

## 2. 任务单元（按推荐执行顺序）

> **统一约定**
> - 修改后必须执行 `cd build && ninja` 与 `cd web && npx tsc --noEmit`；任一失败即视为本任务未完成
> - 每个任务建议产生一个独立 commit，commit message 模板见每条
> - 涉及 proto 修改后必须重新构建 `build/proto/*.pb.cc` 并保证 C++ 编译通过；前端 protobuf 类型由 `web/src/api/types.ts` 手工镜像维护，需同步
> - 不修改无关文件、不重排导入、不改动既有命名风格

---

### A1. 扩展 monitor/NodeInfo / AccountInfo / AliasInfo / GroupInfo / SourceInfo

- **目的**：让 `proto/caster/monitor/*` 真正成为 HTTP 响应的 schema 来源。
- **改动文件**：`proto/caster/monitor/{NodeInfo,AccountInfo,AliasInfo,GroupInfo,SourceInfo}.proto`
- **要求**：
  - `NodeInfo`：在现有字段基础上补齐 `hostname/listen_port/http_port/process_id/http_enabled/uptime_sec/online/master`，与 `handle_get_monitor_cluster` 的 JSON 字段一一对应
  - `AccountInfo`：`account/type/state/connection_limit/contact_person/contact_info/create_time/expire_time/usage_total/remark/active_count`
  - `AliasInfo`：`alias_mpt/source_mpt/strategy/enable/create_time/update_time/remark`
  - `GroupInfo`：`group_id/name/strategy/item_count/account_count/mount_count/remark`
  - `SourceInfo`：`mpt/format/format_details/carrier/nav_system/network/country/latitude/longitude/nmea/solution/generator/compr_encrp/authentication/fee/bitrate`
  - 字段编号严格保持向后兼容（已有字段保留原 number）
- **验收**：`build && ninja` 通过；`bin/Debug/protoc --version` 可用，新生成 pb 头文件包含上述字段
- **commit**：`proto(monitor): expand schemas to mirror runtime payloads`

### A2. 新增 monitor/AuditEntry / RingLogEntry / NodeConfig / RedisStatPoint

- **改动文件**：在 `proto/caster/monitor/` 新建：
  - `AuditEntry.proto`：`id, timestamp, actor, source_ip, action, target_type, target_id, payload(string, JSON), result, error_message`
  - `RingLogEntry.proto`：`timestamp, level(int), category, message`
  - `NodeConfig.proto`：包装一个 oneof：`{ caster_core | service_setting | auth_verify }` 三类配置，配套 `op_seq` 与 `version` 用于乐观并发
  - `RedisStatPoint.proto`：`t, used_memory, total_keys, ops_per_sec, hits, misses, connected_clients`
- **同时更新**：`proto/CMakeLists.txt` 列表
- **验收**：`ninja` 通过；可在 C++ 中 `#include "monitor/AuditEntry.pb.h"` 不报错
- **commit**：`proto(monitor): add audit/log/config/redis-stat schemas`

### A3. 在 proto/caster/core/CasterNode 添加 v3 状态字段

- **改动文件**：`proto/caster/core/CasterNode.proto`
- **新增字段**（追加在尾部，新编号 27~30）：
  - `uint32 sse_clients = 27;` 当前 SSE 连接数
  - `uint32 process_threads = 28;` 进程线程数
  - `uint64 last_audit_seq = 29;` 该节点最近一次审计事件序号
  - `string log_level = 30;` 当前日志级别 (`trace/debug/info/warn/error`)
- **同步**：`caster_internal::keep_alive_callback` 填充这些字段（后续任务 B6/B5 会用到，本任务仅 schema + 默认值占位即可）
- **commit**：`proto(core): extend CasterNode with v3 runtime fields`

### A4. 前端 TS 类型同步

- **改动文件**：`web/src/api/types.ts`
- **要求**：在 `CasterNode` 增加 A3 字段；新增 `AuditEntry / RingLogEntry / RedisStatPoint / NodeConfig` 类型；扩展 `AccountRecord` 的可选字段使其与 A1 对齐
- **验收**：`web && npx tsc --noEmit` 通过
- **commit**：`web(api): sync types with proto v3 expansion`

---

### B1. 审计日志写入框架

- **目的**：所有"写"类 HTTP 请求统一记录到 `LOG:AUDIT`。
- **改动文件**：
  - 新建 `src/http/audit_log.h/.cpp`（HTTP 线程内）
  - `src/http/http_handler.cpp` 在 `route_request` 完成调用后追加：若 method ∈ {POST,PUT,DELETE}，调用 `audit_log::write(req, resp, actor)`
- **数据格式**：
  - Redis Key：`LOG:AUDIT`（List，LPUSH+LTRIM 上限 50000）；同时 LPUSH 到 `LOG:AUDIT:DAILY:<yyyymmdd>`（保留 90 天）
  - 字段：`AuditEntry` proto 的 JSON 形式
- **要求**：
  - 不写读取 `body` 中的密码字段（`password / token` 在 payload 中替换为 `***`）
  - actor 从 token 反查（与 `validate_token` 复用）；失败时 actor=`anonymous`
- **验收**：手动 `curl` 一次 PUT 后 `LRANGE LOG:AUDIT 0 0` 可见 JSON
- **commit**：`feat(http): write audit log for all mutating endpoints`

### B2. 审计日志查询 API

- **改动文件**：`src/http/http_handler.{h,cpp}`、`register_handlers()` 表
- **新增**：
  - `GET /api/audit?limit=200&action=&actor=&start=&end=` → 倒序返回 `LOG:AUDIT` 中匹配条目
  - 支持 `cursor`：返回 `next_cursor` 用于翻页
- **实现**：单线程 Redis `LRANGE` 取一段后内存过滤；最大 `limit=1000`
- **验收**：`curl -H "Authorization: Bearer ..." /api/audit?limit=10` 返回 JSON 数组
- **commit**：`feat(http): add /api/audit query endpoint`

### B3. 进程内环形日志缓冲（spdlog ringbuffer sink）

- **改动文件**：
  - `src/base/log_init.cpp`（若不存在则在 `src/service/main.cpp` 现有日志初始化处）追加注册一个 `spdlog::sinks::ringbuffer_sink_mt`，容量 5000
  - 新建 `src/http/ring_log_view.{h,cpp}`：暴露 `last_n(int n, level_filter)` 函数
  - 路由：`GET /api/logs/ring?n=500&level=warn`
- **验收**：能拿到最近若干条结构化日志（含 level/time/msg）
- **commit**：`feat(log): expose in-process ring buffer via /api/logs/ring`

### B4. Redis 监控时间序列采样

- **改动文件**：
  - `src/http/http_handler.cpp` 注册一个 60s 周期的 `event_new(_http_base, EV_PERSIST)`：
    - 调用 `sync_redis::INFO` + `DBSIZE` + 关键 keyspace 计数
    - LPUSH 到 `MONITOR:REDIS:HISTORY`，LTRIM 到 1440 (一天)
- **新增 API**：`GET /api/monitor/redis/history?range=1h|6h|24h`
- **验收**：等待 ~2 个采样周期后接口返回 ≥1 条数据
- **commit**：`feat(monitor): sample redis stats into MONITOR:REDIS:HISTORY`

### B5. 节点远程日志级别接口

- **改动文件**：
  - 共享内存方案：`src/base/log_init.cpp` 提供 `set_level(const std::string&)`；写入一个 `std::atomic<int>` 让 sink 读取
  - HTTP API：`POST /api/nodes/:id/log-level`，body `{level:"debug"}`；当 `:id` 与本节点 `_node_ID` 匹配时直接生效；否则通过 `caster_internal` 的 Redis `PUBLISH caster::ctrl::<id>` 通道转发，目标节点订阅后调用本地 `set_level`
- **要求**：未知 `id` 返回 404；非法 level 返回 400
- **验收**：`curl -X POST .../api/nodes/<self>/log-level -d '{"level":"debug"}'` 后日志多出 debug 行
- **commit**：`feat(node): remote log-level mutation endpoint`

### B6. 节点配置热更新接口

- **改动文件**：
  - `src/http/http_handler.cpp` 新增：
    - `GET /api/nodes/:id/config`：从 Redis `CONF:CORE/SERVICE/AUTH` 读取并以 `NodeConfig` JSON 返回
    - `PUT /api/nodes/:id/config`：写入 Redis 并 `PUBLISH caster::ctrl::<id> {"op":"reload","section":"core"}`
  - `src/core/src/caster_internal.cpp` 控制订阅回调中处理 `reload`：调用现有 `reload_*` 方法（若不存在仅记录 audit 日志，B7 中真正实现 reload 行为，本任务只完成"接收 + 日志"）
- **验收**：PUT 之后日志中可见 `[caster_internal] received reload signal section=core`
- **commit**：`feat(node): config get/put endpoints with reload broadcast`

### B7. caster_internal 订阅控制通道

- **改动文件**：`src/core/src/caster_internal.{h,cpp}`
- **要求**：
  - `Init` 中新增 `redisAsyncCommand(_sub_context, ..., "SUBSCRIBE caster::ctrl::%s", _node_ID.c_str())`
  - 回调中根据 `op` 字段执行：`reload` / `pause` / `resume` / `disconnect`（`pause/resume` 操作 listener accept 状态；`disconnect` 调用 `kick_*`）
  - 所有动作均 `LOG_INFO` 一行 + 若 audit 系统已就绪则写一条 `AuditEntry`（`actor=remote`）
- **验收**：手动 `redis-cli PUBLISH caster::ctrl::<id> '{"op":"pause"}'` → 新连接被拒；`{"op":"resume"}` 恢复
- **commit**：`feat(core): subscribe per-node control channel`

---

### C1. SSE channel 匹配从子串改为精确集合

- **背景**：`sse_manager::broadcast` 用 `find(event_name)` 判断订阅，存在子串误匹配风险（如 `pull_records` vs `pull_records_*`）。
- **改动文件**：`src/http/sse_manager.{h,cpp}`
- **要求**：在 `add_client` 阶段把 CSV channels 解析为 `std::unordered_set<std::string>` 并存入 `SseClient`；`broadcast` 改用集合 lookup；保留 `"*"` 语义
- **验收**：`ninja` 通过；前端无感（功能保持）
- **commit**：`fix(sse): match channels by exact set lookup`

### C2. 缓存差异化为按 channel 独立 dirty 标记

- **背景**：当前每个 channel 缓存 hash 副本占用 RSS。
- **改动文件**：`src/http/sse_manager.cpp`
- **要求**：`poll_and_broadcast` 用 `std::hash` 计算 `data.dump()` 的 64-bit 哈希存为 `cached_hash`，仅在哈希变化时才广播；`add_client` 初次仍发送完整快照
- **验收**：观察日志确认 broadcast 频率与 hash 变化一致
- **commit**：`perf(sse): switch cache compare to fast hash`

### C3. 客户端连接计数与上限

- **改动文件**：`src/http/sse_manager.{h,cpp}` + `http_handler.cpp`
- **要求**：在 `add_client` 前检查上限（默认 200，可由 `_http_api_config.max_sse_clients` 配置）；超限返回 429；`client_count()` 暴露到 `GET /api/system/status`
- **验收**：`/api/system/status` 中包含 `sse_clients` 字段
- **commit**：`feat(sse): enforce client cap and report count`

---

### D1. ServerDetail 选项卡化

- **改动文件**：`web/src/pages/ServerDetail.tsx`
- **要求**：拆分为 `Tabs(["概览","订阅者","数据流","历史"])`；
  - 概览：保留现有 Descriptions + Statistic
  - 订阅者：调用 `getMountpointSubscribers(mpt)` 实现表格（已存在 API），每行 `{account, ip, online_time}` 并 link 到 `ClientDetail`
  - 数据流：使用 `recharts AreaChart` 显示最近 5 min 速率（来自 `streams` SSE 缓冲在 `useRef` 中）
  - 历史：现有 `ConnectionHistoryTable`
- **验收**：`tsc --noEmit` 通过；导航无 console error
- **commit**：`feat(web): tabify ServerDetail with subscribers and flow chart`

### D2. ClientDetail 选项卡化 + 位置卡片

- **改动文件**：`web/src/pages/ClientDetail.tsx`
- **要求**：Tabs `["概览","定位","数据流","历史"]`；
  - 定位：基于 `ecef_x/y/z` 转 LLA，展示纬度/经度/高程；地图 placeholder 用纯 SVG 散点图（避免引入 leaflet 依赖）
  - 其他同 D1 风格
- **commit**：`feat(web): tabify ClientDetail with position panel`

### D3. NodeDetail 增加运行时信息卡片

- **改动文件**：`web/src/pages/NodeDetail.tsx`
- **要求**：图表上方新增 `Descriptions` 显示 `hostname/listen_port/http_port/pid/log_level/uptime/online_time`；`log_level` 字段右侧加 `Select` + `保存`，调用 B5 接口
- **commit**：`feat(web): node detail runtime panel + log-level switcher`

### D4. 新增审计日志页 `/audit`

- **改动文件**：
  - `web/src/pages/AuditLog.tsx`（新）
  - `web/src/api/index.ts`：`getAuditLog(params)` 包装
  - `web/src/router.tsx` + `MainLayout.tsx` 在"系统"分组下加菜单项
- **要求**：DataTable + 时间/操作人/动作筛选；点击展开 payload JSON
- **commit**：`feat(web): add audit log page`

### D5. 新增运行日志页 `/logs/ring`

- **改动文件**：
  - `web/src/pages/RingLog.tsx`（新）
  - 调用 B3 接口；level 多选；每 5s 轮询；DataTable
- **commit**：`feat(web): add in-process ring log viewer`

### D6. 系统设置完整表单

- **改动文件**：`web/src/pages/Settings.tsx`（已存在但仅占位）
- **要求**：
  - 三个 Card 分别承载 Caster_Core / Service_Setting / Auth_Verify
  - 字段从 B6 `GET /api/nodes/:id/config` 拉取，根据返回 schema 动态渲染（基础类型用 `Input/InputNumber/Switch`，未知子对象渲染 `JSON Editor` 文本框）
  - 保存调用 PUT；成功后刷新；失败显示后端 `error_message`
- **commit**：`feat(web): full settings form with hot-reload`

### D7. 数据统计页对接 stats 接口

- **改动文件**：`web/src/pages/Statistics.tsx`
- **要求**：
  - 顶部 4 个 MetricCard（接收总流量 / 发送总流量 / 用户数 / 基站数）
  - 两个 BarChart：用户排行 Top10 / 挂载点排行 Top10
  - 调用 `/api/stats/overview` + `/api/stats/top-users` + `/api/stats/top-mounts`（前端缺失，B 阶段已有 overview，本任务后端补齐 top-* 时若无则 mock）
- **commit**：`feat(web): connect statistics page to backend`

### D8. Dashboard 新增"集群事件"小卡

- **改动文件**：`web/src/pages/Dashboard.tsx`
- **要求**：右下增加一个 `Card`，显示来自 `LOG:NODE:*` 的最近 10 条事件（master 切换、http 启停）；调用新增 `GET /api/system/events`（后端新接口，从 `LOG:NODE:*` 聚合，限制 100）
- **commit**：`feat(web): dashboard cluster event feed`

---

### E1. 错误边界覆盖率

- **改动文件**：`web/src/router.tsx` 把 `<ErrorBoundary>` 包到每个 Route 的 element 上
- **commit**：`chore(web): wrap routes with ErrorBoundary`

### E2. 骨架屏 Loading

- **改动文件**：所有详情页（D1~D3）首屏未连接时使用 `Skeleton` 替代 `Spin`
- **commit**：`ui(web): switch detail pages to Skeleton loading`

### E3. 主题切换占位

- **改动文件**：`web/src/main.tsx` + `MainLayout.tsx` Header 增加 `Switch`（暗/亮）；亮主题暂时只切换 Ant Design `theme.algorithm`
- **commit**：`ui(web): theme switcher (dark/light)`

### E4. SSE 连接状态可视化

- **改动文件**：`web/src/layouts/MainLayout.tsx`
- **要求**：Header 增加一个小圆点 + tooltip 显示 SSE `connected` 状态；从 `useMultiSSE` 提升状态用 Context
- **commit**：`ui(web): visualize SSE connection status in header`

### E5. 端到端 smoke 脚本

- **改动文件**：`deploy/scripts/e2e_smoke.sh`（新）
- **要求**：使用 `curl` + `jq` 顺序调用 login/system/status/monitor/cluster/audit/logs/ring，全部 200 即成功；非 0 退出
- **commit**：`chore(ci): add e2e smoke script`

---

## 3. 验收门槛

每个任务单元提交前必须满足：
1. `cd build && ninja` 0 警告 0 错误
2. `cd web && npx tsc --noEmit` 无输出
3. 对涉及 Redis 的任务：本地 `redis-cli MONITOR` 抽样确认命令符合预期
4. 不修改 `bin/Debug/conf/*.yml` 的现网默认值
5. 任意接口变更必须同步更新 `doc/api-reference.md`（V3 新增任务里默认包含此项，记得做）

## 4. 风险与回滚

- B1/B2 写入 `LOG:AUDIT` 体量较大，必须配 LTRIM 上限；上线前观察 `MEMORY USAGE LOG:AUDIT`
- B5/B6/B7 引入跨节点控制通道，需确保 `SUBSCRIBE caster::ctrl::*` 路径在 Master 漂移时仍工作（`_sub_context` 重连后必须重新 SUBSCRIBE）
- D6 `JSON Editor` 落盘错误的配置可能导致节点拒绝启动，需在保存前校验 yaml 解析（前端用 `js-yaml`，后端再次用 `yaml-cpp` 校验）

## 5. 任务推荐排程

| 阶段 | 任务 | 备注 |
|------|------|------|
| W1 | A1, A2, A3, A4 | proto + 类型同步，无业务逻辑 |
| W2 | B1, B2, C1, C2 | 审计 + SSE 鲁棒性 |
| W3 | B3, B4, B5, C3 | 监控数据源 + 远程日志级别 |
| W4 | B6, B7 | 节点配置热更新闭环 |
| W5 | D1, D2, D3 | 详情页 Tab 化 |
| W6 | D4, D5, D6, D7, D8 | 全部新页面 |
| W7 | E1~E5 | 体验打磨 + smoke |

每周末执行 `e2e_smoke.sh` + 手工 5 min 回归。
