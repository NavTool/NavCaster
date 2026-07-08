# v2 Caster Minimal NTRIP Data Path

任务：NC-085-v2-caster-minimal-ntrip-data-path

状态：最小 source/client 本地 fan-out 链路

## 范围

本任务在 NC-084 Runtime/Worker 骨架上补齐最小数据面：

- `AcceptorSession` 读取 NTRIP/HTTP header，解析 `connect_type`、`mount`、`Authorization`、`Ntrip-GGA` 和 header 后 `initial_bytes`。
- `HandoffMessage` 继续以值对象转交 `fd`、`ConnectInfo`、`initial_bytes`、远端地址和接受时间。
- `WorkerCore` 只在 Worker event loop 上创建正式 `SourceSession` / `ClientSession` bufferevent。
- Worker 内维护 `mount -> source/client ids`、source session map 和 client session map。
- source 数据优先 fan-out 给同 Worker 同 mount 的本地 client。
- source 数据写入 Redis bus：`stream:mount:<mount>`，同时保留同 Worker 本地 fan-out。
- client 建立后按 mount 订阅 Redis bus，收到其他 Runtime 发布的数据后只向本 Worker 本地 client fan-out。
- Redis bus 不写旧 `MPT:<mount>` channel/key，不兼容旧 Caster Pub/Sub。
- 慢客户端 output buffer 限制占位为 1 MiB，超限断开并递增 `slow_client_disconnect_count` / `output_buffer_limit_count`。

## 最小协议行为

当前只支持：

```text
POST /<mount> HTTP/1.1
SOURCE ... /<mount>
GET /<mount> HTTP/1.1
```

source 和 client 建连成功后，Worker 回写：

```text
ICY 200 OK\r\n\r\n
```

本切片不实现完整 Auth、计费、供应商、Relay、PG 热路径、旧 protobuf 或旧实时数据包装。

## Redis Bus

最小跨 Runtime 数据面使用普通 Redis Pub/Sub：

```text
channel = stream:mount:<mount>
payload = NCV2BUS1 envelope + raw bytes
```

envelope 包含 `origin_runtime_id`、`mount` 和原始 payload 长度。payload 的 raw bytes 不做
JSON 编码，避免 RTCM/NMEA 二进制内容被转义或截断。订阅端会忽略本 runtime 发布的消息，
因此同 Runtime 的本地 client 不依赖 Redis 回环收包。

新增 metrics：

```text
fanout_write_count                  本地 source -> 本地 client 写次数
redis_publish_count                 source 数据发布尝试次数
redis_publish_bytes                 source 数据发布字节数
redis_publish_error_count           publish 失败次数
redis_subscribe_message_count       收到其他 Runtime 数据消息次数
redis_subscribe_bytes               收到其他 Runtime 数据字节数
redis_remote_fanout_write_count     Redis 订阅数据 -> 本地 client 写次数
redis_remote_fanout_bytes           Redis 订阅数据 -> 本地 client 字节数
redis_error_count                   Redis connect/subscribe/publish/decode 错误总数
redis_subscribed_mount_count        当前 Worker 已订阅 mount 数
```

## 可复现 Smoke

构建：

```powershell
$env:NINJA_EXE='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
.\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
```

最小 NTRIP source/client：

```powershell
.\deploy\scripts\v2_caster_ntrip_smoke.ps1 -NtripPort 42185 -HealthPort 19185 -Mount QA_MOUNT_A
```

双 Runtime Redis Pub/Sub：

```powershell
.\deploy\scripts\v2_caster_redis_pubsub_smoke.ps1 -RedisHost 127.0.0.1 -RedisPort 6379 -Mount QA_V2_REDIS_MOUNT_A
```

脚本记录：

```text
NTRIP 端口：42185
Health/Metrics 端口：19185
输入 payload：NC085_PAYLOAD_source_to_client_0123456789\r\n
期望输出：client 收到完全相同 payload
metrics：同一个 worker 的 source_count=1、client_count=1、fanout_write_count>=1、redis_publish_count>=1
日志目录：build\nc085-smoke
```

双 Runtime smoke 记录：

```text
Runtime A：source 写入 payload，publisher worker 的 redis_publish_count>=1 且 redis_publish_error_count=0
Runtime B：client 订阅同 mount，subscriber worker 的 redis_subscribe_message_count>=1 且 redis_remote_fanout_write_count>=1
期望输出：Runtime B client 收到 Runtime A source 写入的完全相同 payload
日志目录：build\nc093-redis-pubsub-smoke
```
