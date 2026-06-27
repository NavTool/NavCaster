# NC-103 v2 Caster PubSub Capacity Baseline

本文记录 `navcaster-caster` 多 Worker 分片稳定性、Redis Pub/Sub 数据链路和轻量容量基线复跑口径。
本任务只建立可复现 baseline，不承诺 16k 正式容量，不做长期 soak。

## Scope

覆盖：

- `navcaster-caster` Release build 和 `--self-test --worker-count <N>`。
- 单 Runtime NTRIP source/client payload round trip。
- 双 Runtime 真实 Redis Pub/Sub payload round trip。
- 多 Worker mount ownership、local fan-out、remote Pub/Sub fan-out counters。
- 轻量容量字段：runtime count、worker count、source/client count、payload size/rate、fan-out/pubsub counters、CPU/RSS。

不覆盖：

- 16k 连接正式压测。
- 30 分钟或更长 soak。
- AdminService/Web 行为。

## Repro Commands

Windows worktree hydrate and build:

```powershell
powershell -ExecutionPolicy Bypass -File F:\Projects\NavCaster\_team\scripts\HYDRATE_WORKTREE_SUBMODULES.ps1 `
  -WorktreePath F:\Projects\NavCaster\worktrees\backend-core\NC-103-v2-caster-pubsub-capacity-baseline

powershell -ExecutionPolicy Bypass -File .\deploy\scripts\build_ninja.ps1 -BuildType Release -Target navcaster-caster
```

Self-test:

```powershell
.\bin\Release\navcaster-caster.exe --self-test --worker-count 4 --self-test-duration-ms 250
```

Single Runtime NTRIP data path:

```powershell
.\deploy\scripts\v2_caster_ntrip_smoke.ps1 `
  -NtripPort 42215 `
  -HealthPort 19215 `
  -WorkerCount 4 `
  -Mount QA_NC103_SINGLE_A
```

Dual Runtime Redis Pub/Sub data path with a real Redis fixture:

```powershell
.\deploy\scripts\v2_caster_redis_pubsub_smoke.ps1 `
  -RedisHost 127.0.0.1 `
  -RedisPort 16379 `
  -RuntimeANtripPort 42216 `
  -RuntimeAHealthPort 19216 `
  -RuntimeBNtripPort 42217 `
  -RuntimeBHealthPort 19217 `
  -WorkerCount 4 `
  -Mount QA_NC103_REDIS_A
```

Lightweight capacity baseline:

```powershell
.\deploy\scripts\v2_caster_capacity_baseline.ps1 `
  -RedisHost 127.0.0.1 `
  -RedisPort 16379 `
  -RuntimeCount 2 `
  -WorkerCount 1,4 `
  -SourceCount 2 `
  -ClientsPerSource 3 `
  -PayloadSizeBytes 128 `
  -PayloadRateHz 1 `
  -DurationSec 20 `
  -SampleIntervalSec 5 `
  -OutDir .\build\nc103-capacity-baseline
```

The capacity script writes one `capacity-baseline.json` per worker-count scenario under `OutDir`.
It requires a reachable real Redis fixture even for `RuntimeCount 1`, because the baseline starts
`navcaster-caster` with Redis Pub/Sub enabled and records Redis publish/error counters.

## Metrics Contract

`GET /metrics` exposes runtime-level counters plus worker detail:

| Field | Meaning |
| --- | --- |
| `runtime_id` | Runtime ID passed by CLI. |
| `worker_count` | Running worker event loops. |
| `mount_count` | Known mount ownership entries. |
| `connection_count` | Runtime source + client sessions. |
| `source_count` / `client_count` | Runtime source/client session counts. |
| `fanout_write_count` | Local source to local client writes. |
| `redis_publish_count` / `redis_publish_error_count` | Source payload publish attempts and errors. |
| `redis_subscribe_message_count` | Pub/Sub payloads accepted from Redis. |
| `redis_remote_fanout_write_count` | Redis payload to local client writes. |
| `redis_subscribed_mount_count` | Active worker mount subscriptions. |
| `slow_client_disconnect_count` / `output_buffer_limit_count` | Slow client isolation counters. |
| `mount_owners[]` | Mount to worker ownership map. |
| `workers[]` | Per-worker counters for ownership and fan-out attribution. |

Ownership expectations:

- Single Runtime same mount source/client must land on the same owner worker.
- Dual Runtime Runtime A source and local client must share the Runtime A owner worker.
- Dual Runtime Runtime B remote client must share the Runtime B owner worker.
- Runtime A increments local `fanout_write_count` and `redis_publish_count`.
- Runtime B increments `redis_subscribe_message_count` and `redis_remote_fanout_write_count`.

## Capacity Report Fields

Each `capacity-baseline.json` records:

- commit, start/finish timestamps, duration.
- runtime_count, worker_count, source_count, clients_per_source.
- local_clients_per_source and remote_clients_per_source.
- payload_size_bytes, payload_rate_hz_per_source, payloads sent/received, mismatch count.
- Redis endpoint and reachability.
- NTRIP and health ports.
- final Runtime A/B metrics.
- sampled Runtime A/B metrics.
- per-process CPU seconds, CPU percent since prior sample, RSS MB and private memory MB.
- sampling_method, currently PowerShell `Get-Process` deltas sampled every `SampleIntervalSec`.

## Windows Port Notes

Use high, explicit ports for local QA. Recommended NC-103 ports:

- Single Runtime: NTRIP `42215`, health `19215`.
- Dual Runtime: Runtime A NTRIP/health `42216/19216`, Runtime B `42217/19217`.
- Capacity script default: Runtime A `42205/19205`, Runtime B `42206/19206`.
- Redis fixture: prefer `127.0.0.1:16379` when Docker publishes Redis, to avoid colliding with a developer Redis on `6379`.

Before a run, check port use:

```powershell
Get-NetTCPConnection -LocalPort 42205,42206,19205,19206,16379 -ErrorAction SilentlyContinue |
  Select-Object LocalAddress,LocalPort,State,OwningProcess
```

If the local environment has low-port restrictions, corporate endpoint protection, or a service already using
`6379`, keep Redis on `16379` and move NTRIP/health ports within `42000-42999` and `19000-19999`.

## Trust Level

This baseline is suitable for NC-105/NC-106 repeatability and regression comparison. It is not a production capacity
claim because it uses small source/client counts, short duration, local loopback networking and one Redis fixture.

## NC-103 Local Run

Environment:

- Windows 10.0.26200, MSVC 2022 Community 17.14.34, Ninja via WinGet link.
- Real Redis fixture reachable at `127.0.0.1:16379`.
- Local loopback only.

Results from 2026-06-27:

| Check | Result | Notes |
| --- | --- | --- |
| `build_ninja.ps1 -BuildType Release -Target navcaster-caster` | Not cleanly closed | Configure succeeded three times, but `cmake --build` stayed idle with only `cmake.exe`/`ninja.exe` alive; stopped manually. |
| Direct VS env Ninja build | PASS | `cmd /c ""VsDevCmd.bat" -arch=x64 -host_arch=x64 && "ninja.exe" -C build\ninja-Release navcaster-caster"` completed and linked `bin\Release\navcaster-caster.exe`. |
| Self-test | PASS | `.\bin\Release\navcaster-caster.exe --self-test --worker-count 4 --self-test-duration-ms 250`. |
| Single Runtime NTRIP smoke | PASS | Ports `52015/55215`, `worker_count=4`, `fanout_write_count=1`, `redis_publish_count=1`. |
| Dual Runtime Redis Pub/Sub smoke | PASS | Redis `127.0.0.1:16379`, ports `52016/55216` and `52017/55217`, local and remote payloads matched. |
| Capacity baseline smoke | PASS | Runtime count 2, worker count 4, source count 2, clients per source 3, payload size 128, rate 1 Hz/source, duration 6 s. |
| `git diff --check` | PASS | No whitespace errors. |

Capacity smoke observed:

- `payloads_sent=12`, `payloads_received=36`, mismatch count 0.
- Runtime A: `fanout_write_count=12`, `redis_publish_count=12`, `redis_publish_error_count=0`.
- Runtime B: `redis_subscribe_message_count=12`, `redis_remote_fanout_write_count=24`, `redis_error_count=0`.
- Report: `build\nc103-capacity-baseline\workers-4-rt-2-src-2-clients-3\capacity-baseline.json`.

Port observation:

- Native `TcpListener` and `navcaster-caster` both failed to bind `42315`/`42415` on this Windows host even though `netstat` did not show listeners.
- `52015+` for NTRIP and `55215+` for health worked reliably in this run.
- NC-106 should re-check port availability on its integration host before using the documented defaults.
