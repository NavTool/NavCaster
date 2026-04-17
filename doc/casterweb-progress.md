# CasterWeb 开发进展与下一步计划

> 生成日期: 2026-04-17
> 背景: 从 Qt6/QML CasterMonitor 迁移到 React Web 架构，CasterMonitor 已完全移除。

---

## 一、已完成工作

### Phase 1: CasterService HTTP API 服务器 (已完成)
- 在 CasterService 中内嵌 HTTP 服务器 (libevent)
- 实现 JWT 认证 (login/logout)
- 实现全部 Redis HASH 资源的 RESTful CRUD API (13 个资源端点)
- 实现 SSE (Server-Sent Events) 实时推送 (13 个通道，2 秒轮询 Redis)
- 配置文件: `HTTP_API_Setting` (端口/CORS/Admin/Web_Root)

### Phase 2: CasterWeb 前端基础 (已完成)
- 技术栈: React 18 + TypeScript + Ant Design 6 + Vite 5
- 路由: HashRouter, 10 条路由, RequireAuth 守卫
- API 层: axios 实例 + Token 管理 + 通用 CRUD 工厂 (`createHashApi<T>`)
- 实时数据: `useSSE` / `useMultiSSE` hooks + `usePolling` 回退
- 格式化工具: 与原 QML 端一致 (formatBytes, formatOnlineTime 等)

### Phase 3: 全部页面实现 (已完成)
| 页面 | 路由 | 数据模式 | 操作 |
|------|------|---------|------|
| Dashboard | /dashboard | SSE (nodes+servers+clients) | 只读 |
| Servers | /servers | SSE (servers) | 只读 |
| Clients | /clients | SSE (clients) | 只读 |
| Accounts | /accounts | Polling 3s | 完整 CRUD |
| Sources | /sources | Polling 3s | 完整 CRUD |
| Aliases | /aliases | Polling 3s | 完整 CRUD |
| AccessGroups | /access | Polling 3s | 完整 CRUD |
| PullRelay | /relay/pull | Polling 3s | 完整 CRUD |
| PushRelay | /relay/push | Polling 3s | 完整 CRUD |
| Login | /login | — | 认证 |

### Phase 4: 暗色主题 (Spark 风格融合) (已完成)
- AntDesign ConfigProvider + darkAlgorithm + 自定义 Token
- 配色: 主色 `#4a8eff`, 背景 `#141625`, 卡片 `#1e2235`
- StatusIndicator 组件 (脉冲动画状态点)
- Dashboard: Spark 风格 Metric 卡片 + 节点详情卡片
- MainLayout: 品牌化暗色侧边栏
- Login: 暗色主题登录页
- Servers/Clients: 表格增强样式

### Phase 5: CasterMonitor 移除 (已完成)
- 删除 `app/CasterMonitor/` 目录
- 删除 `imports/CasterMonitor/`, `imports/FluentUI/`
- 删除 `rely/FluentUI-Pro/`
- 清理 CMakeLists.txt (移除 BUILD_CASTER_MONITOR, FluentUI 依赖)
- 更新架构文档

---

## 二、当前代码结构

```
app/CasterWeb/src/
├── api/
│   ├── auth.ts          # login/logout
│   ├── client.ts        # axios + Token + setBaseURL
│   ├── index.ts         # 13 个资源 API 实例
│   ├── resource.ts      # createHashApi<T> / createReadOnlyHashApi<T> 工厂
│   └── types.ts         # 全部 TS 类型 (283 行)
├── components/
│   └── StatusIndicator.tsx  # 状态指示灯
├── hooks/
│   ├── usePolling.ts    # 固定间隔轮询
│   └── useSSE.ts        # SSE 实时订阅 (单通道/多通道)
├── layouts/
│   └── MainLayout.tsx   # Sider + Header + Content
├── pages/
│   ├── Dashboard.tsx    # 仪表盘 (SSE)
│   ├── Servers.tsx      # 基准站 (SSE, 只读)
│   ├── Clients.tsx      # 移动站 (SSE, 只读)
│   ├── Accounts.tsx     # 账号管理 (Polling + CRUD)
│   ├── Sources.tsx      # 源列表 (Polling + CRUD)
│   ├── Aliases.tsx      # 别名 (Polling + CRUD)
│   ├── AccessGroups.tsx # 访问组 (Polling + CRUD)
│   ├── PullRelay.tsx    # Pull 中继 (Polling + CRUD)
│   ├── PushRelay.tsx    # Push 中继 (Polling + CRUD)
│   └── Login.tsx        # 登录
├── utils/
│   └── format.ts        # 格式化工具
├── router.tsx           # HashRouter + RequireAuth
├── App.tsx              # ConfigProvider 暗色主题
├── index.css            # 全局暗色 CSS
└── main.tsx             # 入口
```

---

## 三、后端 API 端点清单

### 认证
| 方法 | 端点 | 说明 |
|------|------|------|
| POST | `/api/auth/login` | 登录 → JWT Token |
| POST | `/api/auth/logout` | 登出 |

### CRUD 资源 (完整 GET/POST/PUT/DELETE)
| 端点 | Redis 键 | 说明 |
|------|---------|------|
| `/api/accounts` | `ACT:RECORD` | 账号 |
| `/api/sources` | `MPT:RECORD` | 源表 |
| `/api/aliases` | `ALIAS:RULE` | 别名规则 |
| `/api/access/groups` | `ACCESS:GROUP` | 访问组 |
| `/api/access/items/{gid}` | `ACCESS:ITEM:{gid}` | 访问项 |
| `/api/relays/pull` | `PULL:RECORD` | Pull 中继配置 |
| `/api/relays/push` | `PUSH:RECORD` | Push 中继配置 |

### 只读资源 (GET only)
| 端点 | Redis 键 | 说明 |
|------|---------|------|
| `/api/servers` | `MPT:STAT` | 基准站状态 |
| `/api/clients` | `USR:STAT` | 移动站状态 |
| `/api/streams` | `STR:STAT` | 数据流状态 |
| `/api/nodes` | `CASTER:NODE` | 集群节点 |
| `/api/accounts/active` | `STR:ACTIVE` | 活跃会话 |
| `/api/relays/pull/status` | `PULL:STAT` | Pull 状态 |
| `/api/relays/push/status` | `PUSH:STAT` | Push 状态 |
| `/api/status` | — | 系统状态 |
| `/api/status/health` | — | 健康检查 |

### SSE
| 端点 | 通道 |
|------|------|
| `/api/events/stream?token=xxx&channels=xxx` | servers, clients, streams, nodes, accounts, sources, aliases, access_groups, pull_records, pull_states, push_records, push_states, account_actives |

---

## 四、下一步工作计划

### P0: 代码结构优化

#### 4.1 CRUD 页面提取通用组件
**问题**: Accounts/Sources/Aliases/AccessGroups/PullRelay/PushRelay 六个页面结构高度重复 (Table+Modal+Form+新增+编辑+删除)。
**方案**: 提取 `CrudPage<T>` 通用组件:
```tsx
<CrudPage<AccountRecord>
  title="账号管理"
  icon={<TeamOutlined />}
  api={accountsApi}
  columns={columns}
  formFields={formFields}
  pollingInterval={3000}
/>
```
**工作量**: 约 1-2 天
**优先级**: 中 (不影响功能，提升维护性)

#### 4.2 Polling 页面迁移到 SSE
**问题**: CRUD 页面使用 3s Polling，但后端已支持对应 SSE 通道。
**方案**: 将 CRUD 页面的数据获取从 `usePolling` 切换到 `useSSE`，仅 CRUD 操作使用 REST API。SSE 能减少无效请求、降低延迟。
**工作量**: 约 0.5 天
**优先级**: 中

#### 4.3 Vite 开发代理配置
**问题**: `vite.config.ts` 无 proxy，开发时 API 请求需要手动设置后端地址。
**方案**:
```ts
server: {
  proxy: {
    '/api': { target: 'http://localhost:8080', changeOrigin: true }
  }
}
```
**工作量**: 5 分钟
**优先级**: 高 (立即改善开发体验)

---

### P1: 功能完善

#### 4.4 账号管理增强
当前 Accounts 页面已有完整 CRUD，需补充:
- [ ] **密码强度校验**: 前端 Form 添加密码强度规则
- [ ] **账号过期管理**: 添加 `expire` 字段的日期选择器
- [ ] **批量操作**: 表格多选 + 批量启用/禁用/删除
- [ ] **搜索/过滤**: 添加账号名/联系人搜索框
- [ ] **活跃会话查看**: 使用 `accountActivesApi` 展示当前在线用户
- [ ] **踢出在线用户**: (需后端新增端点)

#### 4.5 数据转发任务管理增强
PullRelay/PushRelay 已有 CRUD，需补充:
- [ ] **状态实时展示**: 将 `pullStatesApi`/`pushStatesApi` 数据合并到表格中显示 (在线/离线/连接中)
- [ ] **启用/停用控制**: 添加 enable/disable 操作 (需后端支持)
- [ ] **连接日志**: 显示最近连接/断开事件
- [ ] **重连操作**: 手动触发重连按钮 (需后端新增端点)
- [ ] **批量操作**: 批量启用/停用/删除

#### 4.6 访问控制项 (AccessItems) UI
**问题**: `accessItemsApi` 已定义但无对应 UI。
**方案**: 在 AccessGroups 页面添加展开行或跳转链接，管理每个分组下的访问项 (挂载点级别的可见/可访问/最近点权限)。
**工作量**: 约 0.5 天

#### 4.7 数据流统计页面
**问题**: `streamsApi` 已定义但无页面。
**方案**: 添加 Streams 页面，展示每个挂载点的实时流量统计。
**工作量**: 约 0.5 天

#### 4.8 系统状态页面
**问题**: `getSystemStatus()` 已定义但无页面。
**方案**: 在 Dashboard 或新页面中展示 Redis 连接状态、系统资源、服务配置等。
**工作量**: 约 0.5 天

---

### P2: 健壮性提升

#### 4.9 全局错误处理
- [ ] SSE 页面添加 `error` 状态提示 (连接断开/重连中)
- [ ] API 请求失败统一 Toast 提示 + 详细错误信息
- [ ] 网络断开检测 + 自动重连指示器

#### 4.10 表单校验增强
- [ ] 所有 CRUD 表单添加完整的前端校验规则
- [ ] IP 地址格式校验
- [ ] 端口范围校验 (1-65535)
- [ ] 挂载点名称格式校验 (字母数字下划线)
- [ ] 防止重复提交 (debounce)

#### 4.11 SSE 连接状态指示
- [ ] 在 Header 或侧边栏添加全局连接状态指示器
- [ ] SSE 断开时显示明确提示，而非静默失败
- [ ] 添加手动重连按钮

#### 4.12 加载与空状态
- [ ] 统一骨架屏 (Skeleton) 替代 Spin
- [ ] 自定义空状态插图和提示文案
- [ ] 首次加载与刷新加载区分

#### 4.13 类型安全
- [ ] 修复 `SourceDecordType` 拼写为 `SourceDecodeType`
- [ ] API 响应增加运行时类型校验 (可选 zod)
- [ ] 严格化 TypeScript 配置 (`strict: true` 检查)

---

### P3: 可选增强

#### 4.14 前端测试
- [ ] 关键工具函数单元测试 (format.ts)
- [ ] API 层 Mock 测试
- [ ] 页面组件渲染测试

#### 4.15 部署优化
- [ ] Vite 代码分割 (dynamic import)
- [ ] gzip 压缩
- [ ] CasterService 静态文件服务配置 (Web_Root)
- [ ] Docker 镜像集成 CasterWeb 构建产物

#### 4.16 用户体验
- [ ] 响应式布局优化 (移动端适配)
- [ ] 表格列自定义显示/隐藏
- [ ] 数据导出 (CSV/Excel)
- [ ] 操作确认二次验证 (敏感操作)
- [ ] 快捷键支持

---

## 五、推荐执行顺序

```
第一步: P0 基础优化 (1-2 天)
  ├── 4.3 Vite proxy 配置 (5 分钟)
  ├── 4.1 CrudPage 通用组件提取 (可选，先做 P1 也行)
  └── 4.2 Polling → SSE 迁移

第二步: P1 核心功能 (2-3 天)
  ├── 4.4 账号管理增强 (搜索/过滤/批量/过期/活跃会话)
  ├── 4.5 数据转发增强 (状态合并/启停/重连)
  ├── 4.6 AccessItems UI
  └── 4.7 数据流统计页面

第三步: P2 健壮性 (1-2 天)
  ├── 4.9 错误处理增强
  ├── 4.10 表单校验
  ├── 4.11 SSE 状态指示
  └── 4.13 类型安全

第四步: P3 增强 (按需)
  └── 测试/部署/UX 优化
```

---

## 六、WSL 开发环境迁移注意事项

1. **Git 克隆**: 在 WSL 中重新 `git clone` 仓库（或从 Windows 路径 `/mnt/f/Dev/NavCaster` 复制）
2. **Node.js**: WSL 中需要安装 Node.js 18+ (`nvm install 18`)
3. **依赖安装**: `cd app/CasterWeb && npm install`
4. **开发启动**: `npm run dev`
5. **CasterService 编译**: 需要 CMake 3.21+, GCC/Clang, libevent, hiredis, protobuf 等依赖
6. **Redis**: WSL 中 `sudo apt install redis-server && sudo service redis-server start`
7. **VS Code**: 使用 Remote-WSL 扩展打开工作区，Copilot 会在新对话中工作
8. **本文档**: 新对话中告诉 Copilot "请阅读 doc/casterweb-progress.md 了解项目进展" 即可快速恢复上下文
