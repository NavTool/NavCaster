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
- Redis publish 暂为 Worker 内计数占位：`redis_publish_count` / `redis_publish_bytes`，不写旧 key。
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

脚本记录：

```text
NTRIP 端口：42185
Health/Metrics 端口：19185
输入 payload：NC085_PAYLOAD_source_to_client_0123456789\r\n
期望输出：client 收到完全相同 payload
metrics：同一个 worker 的 source_count=1、client_count=1、fanout_write_count>=1、redis_publish_count>=1
日志目录：build\nc085-smoke
```
