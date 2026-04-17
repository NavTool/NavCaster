# NTRIP 协议对接标准参考文档

> 本文档基于 **RTCM 10410.1 — NTRIP Version 2.0** (2011-06-28 with Amendment 1) 整理，  
> 结合 NavCaster 项目实际实现，供开发和联调参考。

---

## 1. 协议概述

NTRIP（Networked Transport of RTCM via Internet Protocol）是基于 HTTP/1.1 的应用层流式传输协议，用于通过互联网分发 GNSS 差分改正数据。

### 1.1 系统组件

| 组件 | 角色 | NavCaster 对应 |
|------|------|---------------|
| NtripCaster | 中心服务器，接收 Server 数据，分发给 Client | CasterService（主进程） |
| NtripServer | 数据源上传方（基站） | `server_ntrip` carrier |
| NtripClient | 数据接收方（移动站） | `client_ntrip` carrier |

### 1.2 默认端口

TCP **2101**（可配置）

### 1.3 协议版本

| 版本 | 说明 |
|------|------|
| Ntrip/1.0 | 旧版协议，存在 HTTP 规范违反（ICY 200 OK、SOURCE 命令等） |
| Ntrip/2.0 | 符合 HTTP/1.1 规范，支持 chunked、源表过滤、RTSP/RTP 可选 |

NavCaster 必须同时支持 1.0 和 2.0 请求，通过 `Ntrip-Version` 头判断版本。

---

## 2. HTTP 通信（TCP）

### 2.1 NtripClient → NtripCaster：请求源表

#### Ntrip 2.0

```
GET / HTTP/1.1\r\n
Host: <caster_host>\r\n
Ntrip-Version: Ntrip/2.0\r\n
User-Agent: NTRIP <ClientName>/<Version>\r\n
Connection: close\r\n
\r\n
```

**响应：**
```
HTTP/1.1 200 OK\r\n
Ntrip-Version: Ntrip/2.0\r\n
Ntrip-Flags: <flags>\r\n
Server: NTRIP <CasterName>/<Version>\r\n
Date: <HTTP date>\r\n
Connection: close\r\n
Content-Type: gnss/sourcetable\r\n
Content-Length: <length>\r\n
\r\n
<sourcetable data>
ENDSOURCETABLE\r\n
```

#### Ntrip 1.0

```
GET / HTTP/1.0\r\n
User-Agent: NTRIP <ClientName>/<Version>\r\n
\r\n
```

**响应：**
```
SOURCETABLE 200 OK\r\n
Server: NTRIP <CasterName> <Version>/1.0\r\n
Connection: close\r\n
Content-Type: text/plain\r\n
Content-Length: <length>\r\n
\r\n
<sourcetable data>
ENDSOURCETABLE\r\n
```

> **注意：** Ntrip 1.0 中，当客户端请求的挂载点不存在时，也返回源表而非错误码。

### 2.2 NtripClient → NtripCaster：请求 GNSS 数据

#### Ntrip 2.0

```
GET /<mountpoint> HTTP/1.1\r\n
Host: <caster_host>\r\n
Ntrip-Version: Ntrip/2.0\r\n
User-Agent: NTRIP <ClientName>/<Version>\r\n
Authorization: Basic <base64(user:password)>\r\n
Connection: close\r\n
\r\n
```

**响应：**
```
HTTP/1.1 200 OK\r\n
Ntrip-Version: Ntrip/2.0\r\n
Server: NTRIP <CasterName>/<Version>\r\n
Date: <HTTP date>\r\n
Cache-Control: no-store, no-cache, max-age=0\r\n
Pragma: no-cache\r\n
Connection: close\r\n
Content-Type: gnss/data\r\n
Transfer-Encoding: chunked\r\n  (推荐)
\r\n
<GNSS data stream>
```

#### Ntrip 1.0

```
GET /<mountpoint> HTTP/1.0\r\n
User-Agent: NTRIP <ClientName>/<Version>\r\n
Authorization: Basic <base64(user:password)>\r\n
\r\n
```

**响应：**
```
ICY 200 OK\r\n
\r\n
<GNSS data stream>
```

### 2.3 NtripServer → NtripCaster：上传数据

#### Ntrip 2.0

```
POST /<mountpoint> HTTP/1.1\r\n
Host: <caster_host>\r\n
Ntrip-Version: Ntrip/2.0\r\n
Authorization: Basic <base64(user:password)>\r\n
User-Agent: NTRIP <ServerName>/<Version>\r\n
Transfer-Encoding: chunked\r\n  (推荐)
Connection: close\r\n
\r\n
<GNSS data stream>
```

**响应：**
```
HTTP/1.1 200 OK\r\n
Ntrip-Version: Ntrip/2.0\r\n
Server: NTRIP <CasterName>/<Version>\r\n
Date: <HTTP date>\r\n
Connection: close\r\n
\r\n
```

#### Ntrip 1.0

**请求（三种变体均需支持）：**
```
SOURCE <password> /<mountpoint> HTTP/1.1\r\n
Source-Agent: NTRIP <ServerName>/<Version>\r\n
\r\n
```
```
SOURCE <password> /<mountpoint>\r\n
Source-Agent: NTRIP <ServerName>/<Version>\r\n
\r\n
```
```
SOURCE <password> <mountpoint>\r\n
Source-Agent: NTRIP <ServerName>/<Version>\r\n
\r\n
```

**响应（两种均可能）：**
```
ICY 200 OK\r\n
```
或
```
OK\r\n
```

**错误响应：**
```
ERROR - Bad Password\r\n
ERROR - Mount Point Taken or Invalid\r\n
ERROR - Already Connected\r\n
```

### 2.4 NMEA GGA 位置传递

用于虚拟参考站（VRS）等场景，客户端向 Caster 发送自身位置。

- **Ntrip 2.0 首选方式** — 在请求头中传递：
  ```
  Ntrip-GGA: $GPGGA,040941.00,5034.91174,N,...*51\r\n
  ```
- **Ntrip 1.0 兼容方式** — 在请求体（空行后）直接发送：
  ```
  $GPGGA,040941.00,5034.91174,N,...*51\r\n
  ```

客户端可以随时发送更新的 GGA 语句。

---

## 3. 认证

### 3.1 Basic 认证（必须支持）

```
Authorization: Basic <base64(username:password)>\r\n
```

如 `ntrip:secret` 编码为 `bnRyaXA6c2VjcmV0`。

认证失败时 Caster 返回：
```
HTTP/1.1 401 Unauthorized\r\n
WWW-Authenticate: Basic realm="/<mountpoint>"\r\n
...
```

### 3.2 Digest 认证（可选支持）

基于 RFC 2617 的 MD5 摘要认证，更安全。源表 STR 条目的 `<authentication>` 字段标识使用 B（Basic）或 D（Digest）。

---

## 4. Transfer-Encoding: chunked

Ntrip 2.0 **推荐**对所有流式数据传输使用 chunked 编码。所有 Ntrip 2.0 组件**必须**能处理 chunked 编码。

格式：
```
<hex_size>\r\n
<data>\r\n
```

示例：
```
E\r\n
TEST TEST TEST\r\n
13\r\n
TEST TEST TEST TEST\r\n
```

> chunk 大小行后可以有分号分隔的扩展信息，应忽略。

---

## 5. 错误码

| Code | 文本 | 说明 |
|------|------|------|
| 200 | OK | 成功 |
| 401 | Unauthorized | 无认证或认证错误 |
| 404 | Not Found | 挂载点不存在（Ntrip 1.0 返回源表替代） |
| 409 | Conflict | 挂载点已被其他 Server 占用 |
| 500 | Internal Server Error | 内部错误 |
| 501 | Not Implemented | 功能未实现 |
| 503 | Service Unavailable | 过载或带宽限制 |

---

## 6. HTTP 头字段

### 6.1 标准 HTTP 头

| 头字段 | Caster | Server | Client | 说明 |
|--------|--------|--------|--------|------|
| `Authorization` | 不用 | 必须 | 必须 | Basic/Digest 认证 |
| `Cache-Control` | 推荐 | 可选 | 不用 | `no-store, no-cache, max-age=0` |
| `Connection` | 推荐 | 推荐 | 推荐 | `close`（不使用持久连接） |
| `Content-Length` | 可选 | 不用 | 不用 | 仅源表/错误响应使用 |
| `Content-Type` | 推荐 | 不用 | 不用 | `gnss/data`、`gnss/sourcetable`、`text/plain`、`text/html` |
| `Date` | 推荐 | 可选 | 可选 | HTTP 日期格式 |
| `Host` | 可选 | 推荐 | 推荐 | 虚拟主机必需 |
| `Server` | 推荐 | 不用 | 不用 | `NTRIP <Name>/<Version>` |
| `Pragma` | 推荐 | 可选 | 不用 | `no-cache` |
| `Transfer-Encoding` | 推荐 | 推荐 | 不用 | `chunked` |
| `User-Agent` | 不用 | 推荐 | 推荐 | `NTRIP <Name>/<Version>` |
| `WWW-Authenticate` | 必须(401) | 不用 | 不用 | 401 响应时使用 |

### 6.2 NTRIP 专用头

| 头字段 | Caster | Server | Client | 说明 |
|--------|--------|--------|--------|------|
| `Ntrip-Version` | 推荐 | 推荐 | 推荐 | `Ntrip/2.0` 或 `Ntrip/1.0` |
| `Ntrip-GGA` | 不用 | 不用 | 推荐 | VRS 位置（v2.0 首选方式） |
| `Ntrip-STR` | 不用 | 推荐 | 不用 | 源表 STR 条目（从第3字段开始） |
| `Ntrip-Flags` | 推荐 | 不用 | 不用 | Caster 能力标志 |

### 6.3 Ntrip 1.0 废弃头

| 头字段 | 替代 |
|--------|------|
| `STR:` | `Ntrip-STR:` |
| `Source-Agent:` | `User-Agent:` |

---

## 7. 源表（Sourcetable）格式

源表以 `ENDSOURCETABLE\r\n` 结尾，每行一条记录，字段用分号 `;` 分隔。

### 7.1 STR 条目（数据流）

```
STR;<mountpoint>;<identifier>;<format>;<format_details>;<carrier>;<nav_system>;<network>;<country>;<latitude>;<longitude>;<nmea>;<solution>;<generator>;<compr_encryp>;<authentication>;<fee>;<bitrate>;...;<misc>
```

| # | 字段 | 示例 |
|---|------|------|
| 1 | `STR` | STR |
| 2 | `<mountpoint>` | RTCM3 |
| 3 | `<identifier>` | Beijing |
| 4 | `<format>` | RTCM 3.3 |
| 5 | `<format_details>` | 1004(1),1005(5),1012(1) |
| 6 | `<carrier>` | 2 (L1&L2) |
| 7 | `<nav_system>` | GPS+GLONASS+BDS |
| 8 | `<network>` | NavCaster |
| 9 | `<country>` | CHN |
| 10 | `<latitude>` | 39.90 |
| 11 | `<longitude>` | 116.40 |
| 12 | `<nmea>` | 0 (不需要) / 1 (需要) |
| 13 | `<solution>` | 0 (单基站) / 1 (网络) |
| 14 | `<generator>` | NavCaster |
| 15 | `<compr_encryp>` | none |
| 16 | `<authentication>` | B (Basic) / D (Digest) / N (无) |
| 17 | `<fee>` | N (免费) / Y (收费) |
| 18 | `<bitrate>` | 5000 |
| n | `<misc>` | none |

### 7.2 CAS 条目（Caster 信息）

```
CAS;<host>;<port>;<identifier>;<operator>;<nmea>;<country>;<latitude>;<longitude>;<fallback_host>;<fallback_port>;...;<misc>
```

### 7.3 NET 条目（网络信息）

```
NET;<identifier>;<operator>;<authentication>;<fee>;<web_net>;<web_str>;<web_reg>;...;<misc>
```

---

## 8. 实现注意事项

1. **客户端应始终使用 Ntrip 2.0 发请求**，但必须准备接收 1.0 响应。
2. **Caster 必须检测 1.0 和 2.0 请求**并正确响应。非 NTRIP 请求（如浏览器）按 2.0 处理。
3. **Server 端** — 如果配置了用户名，使用 2.0（POST）；否则使用 1.0（SOURCE）。
4. **断线重连** — 必须使用指数退避（如 1→2→4→8→16→32 秒，上限 5 分钟）。
5. **chunked 编码** — Ntrip 2.0 所有流式传输推荐使用。
6. **头字段顺序不固定** — 软件必须能处理任意顺序的头字段。
7. **TLS** — 可选支持，不强制要求。

---

## 9. 协议版本检测逻辑

```
入站请求:
  ├─ 首行以 "SOURCE " 开头          → Ntrip 1.0 Server
  ├─ 首行以 "GET / HTTP/1."        → 源表请求
  │   ├─ 包含 "Ntrip-Version: Ntrip/2.0"  → v2.0 源表
  │   └─ 不包含                             → v1.0 源表
  ├─ 首行以 "GET /<mpt> HTTP/1."   → Client 请求
  │   ├─ 包含 "Ntrip-Version: Ntrip/2.0"  → v2.0 Client
  │   └─ 不包含                             → v1.0 Client
  └─ 首行以 "POST /<mpt> HTTP/1."  → Ntrip 2.0 Server

出站响应:
  ├─ v2.0 源表  → "HTTP/1.1 200 OK" + Content-Type: gnss/sourcetable
  ├─ v1.0 源表  → "SOURCETABLE 200 OK" + Content-Type: text/plain
  ├─ v2.0 数据  → "HTTP/1.1 200 OK" + Content-Type: gnss/data
  ├─ v1.0 数据  → "ICY 200 OK"
  ├─ v2.0 Server → "HTTP/1.1 200 OK"
  └─ v1.0 Server → "ICY 200 OK" 或 "OK"
```

---

*文档版本: 1.0 | 基于 RTCM 10410.1 (Ntrip Version 2.0, Amendment 1) | 生成时间: 2026-04-17*
