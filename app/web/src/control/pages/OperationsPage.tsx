import {
  ApiOutlined,
  CloudServerOutlined,
  ClusterOutlined,
  DatabaseOutlined,
  ExclamationCircleOutlined,
  FieldTimeOutlined,
  FileSearchOutlined,
  LinkOutlined,
  NodeIndexOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  ThunderboltOutlined,
  WarningOutlined,
} from '@ant-design/icons';
import { Alert, Button, Empty } from 'antd';
import type { ReactNode } from 'react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { AdminHealth, HostSummary, RuntimeEvent, RuntimeSummary, WorkerMetric } from '../../api/contracts';
import { controlStatusLabel, eventLevelLabel } from '../labels';
import { formatDateTime } from './useControlPage';

type OpsTone = 'green' | 'blue' | 'orange' | 'red';

type OpsSeries = {
  name: string;
  color: string;
  values: number[];
  fill?: boolean;
};

type OpsData = {
  health: AdminHealth | null;
  hosts: HostSummary[];
  runtimes: RuntimeSummary[];
  workers: WorkerMetric[];
  events: RuntimeEvent[];
};

type Bucket = {
  label: string;
  value: number;
};

type ComponentLoad = {
  id: string;
  name: string;
  role: string;
  status: string;
  tone: OpsTone;
  loadPercent: number;
  primary: string;
  secondary: string;
  detail: string;
  updatedAt: string;
};

function xAt(index: number, total: number, width: number) {
  if (total <= 1) return width / 2;
  return (index * width) / (total - 1);
}

function pathFrom(values: number[], width: number, height: number, top = 4) {
  const safeValues = values.length > 1 ? values : [values[0] ?? 0, values[0] ?? 0];
  const max = Math.max(...safeValues);
  const min = Math.min(...safeValues);
  const range = max - min || 1;
  return safeValues
    .map((value, index) => {
      const x = xAt(index, safeValues.length, width);
      const y = top + height - ((value - min) / range) * height;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');
}

function MiniLine({
  values,
  color = '#14b8a6',
  height = 70,
}: {
  values: number[];
  color?: string;
  height?: number;
}) {
  const width = 340;
  const safeValues = values.length > 0 ? values : [0, 0];
  return (
    <svg className="ops-mini-line" viewBox={`0 0 ${width} ${height + 12}`} aria-hidden="true">
      <polyline fill="none" points={pathFrom(safeValues, width, height)} stroke={color} strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" />
    </svg>
  );
}

function OpsStat({
  title,
  value,
  detail,
  tone,
  icon,
}: {
  title: string;
  value: string;
  detail: string;
  tone: OpsTone;
  icon: ReactNode;
}) {
  return (
    <section className={`ops-stat ops-stat-${tone}`}>
      <div className="ops-stat-title">
        {title}
        <span>{icon}</span>
      </div>
      <strong>{value}</strong>
      <p>{detail}</p>
    </section>
  );
}

function EmptyOpsPanel({ title, subtitle }: { title: string; subtitle: string }) {
  return (
    <section className="ops-panel ops-empty-panel">
      <div className="ops-panel-header">
        <h3>{title}</h3>
        <span>当前快照</span>
      </div>
      <div className="ops-empty-state">
        <FileSearchOutlined />
        <strong>暂无数据</strong>
        <span>{subtitle}</span>
      </div>
    </section>
  );
}

function ThroughputChart({ series, labels, max }: { series: OpsSeries[]; labels: string[]; max: number }) {
  const width = 620;
  const height = 210;
  const hasData = labels.length > 0 && series.some((item) => item.values.some((value) => value > 0));
  const safeMax = Math.max(max, 1);
  const skip = labels.length > 6 ? Math.ceil(labels.length / 6) : 1;

  if (!hasData) {
    return (
      <div className="ops-chart">
        <Empty description="AdminService 当前没有吞吐指标" />
      </div>
    );
  }

  return (
    <div className="ops-chart">
      <div className="ops-chart-legend">
        {series.map((item) => (
          <span key={item.name}><i style={{ borderColor: item.color }} />{item.name}</span>
        ))}
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} aria-label="NTRIP Caster 实时吞吐">
        {[0, 1, 2, 3, 4].map((line) => (
          <line className="ops-chart-grid" key={line} x1="42" x2="590" y1={24 + line * 38} y2={24 + line * 38} />
        ))}
        {series.map((item) => {
          const points = item.values.map((value, index) => {
            const x = 42 + xAt(index, item.values.length, 548);
            const y = 176 - (Math.min(value, safeMax) / safeMax) * 150;
            return `${x.toFixed(1)},${y.toFixed(1)}`;
          });
          return (
            <g key={item.name}>
              {item.fill ? <polygon fill={`${item.color}1f`} points={`42,176 ${points.join(' ')} 590,176`} /> : null}
              <polyline fill="none" stroke={item.color} strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" points={points.join(' ')} />
            </g>
          );
        })}
        {labels.map((label, index) => {
          if (index % skip !== 0) return null;
          return <text className="ops-chart-label" key={`${label}-${index}`} x={42 + xAt(index, labels.length, 548)} y="202">{label}</text>;
        })}
      </svg>
    </div>
  );
}

function DistributionPanel({ title, subtitle, buckets }: { title: string; subtitle: string; buckets: Bucket[] }) {
  const max = Math.max(...buckets.map((item) => item.value), 0);
  if (max === 0) return <EmptyOpsPanel title={title} subtitle={subtitle} />;
  return (
    <div className="ops-panel">
      <div className="ops-panel-header">
        <h3>{title}</h3>
        <span>{subtitle}</span>
      </div>
      <div className="ops-bars">
        {buckets.map((bucket) => (
          <div className="ops-bar" key={bucket.label}>
            <span>{bucket.label}</span>
            <div><i style={{ height: `${Math.max(8, (bucket.value / max) * 160)}px` }} /></div>
            <span>{bucket.value}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

function formatNumber(value: number) {
  return Math.round(value).toLocaleString('zh-CN');
}

function formatBps(value: number) {
  if (value >= 1_000_000) return `${(value / 1_000_000).toFixed(1)} Mbps`;
  if (value >= 1_000) return `${(value / 1_000).toFixed(1)} Kbps`;
  return `${Math.round(value)} bps`;
}

function formatPercent(value: number) {
  return `${Math.round(value)}%`;
}

function loadTone(value: number, failed = false): OpsTone {
  if (failed) return 'red';
  if (value >= 85) return 'red';
  if (value >= 65) return 'orange';
  if (value >= 35) return 'blue';
  return 'green';
}

function clampPercent(value: number) {
  return Math.max(0, Math.min(100, Math.round(value)));
}

function average(values: number[]) {
  if (values.length === 0) return 0;
  return values.reduce((sum, item) => sum + item, 0) / values.length;
}

function hostMemoryPercent(host: HostSummary) {
  return host.memory_total_gb > 0 ? (host.memory_used_gb / host.memory_total_gb) * 100 : 0;
}

function memoryPercent(hosts: HostSummary[]) {
  const used = hosts.reduce((sum, host) => sum + host.memory_used_gb, 0);
  const total = hosts.reduce((sum, host) => sum + host.memory_total_gb, 0);
  return total > 0 ? (used / total) * 100 : 0;
}

function runtimeName(runtimeById: Map<string, RuntimeSummary>, event: RuntimeEvent) {
  return event.runtime_id ? runtimeById.get(event.runtime_id)?.name ?? event.runtime_id : '运行时';
}

function logLevelClass(level: RuntimeEvent['level']) {
  return level === 'warning' ? 'warn' : level;
}

function clampScore(value: number) {
  return Math.max(0, Math.min(100, Math.round(value)));
}

function serviceStatusLabel(value?: string) {
  if (!value) return '未知';
  if (value === 'ok' || value === 'connected' || value === 'running') return '正常';
  if (value === 'configured') return '已配置';
  if (value === 'unavailable' || value === 'failed') return '异常';
  return value;
}

function ComponentLoadCard({ item }: { item: ComponentLoad }) {
  return (
    <section className={`ops-component-card ops-component-${item.tone}`}>
      <div className="ops-component-card-head">
        <div>
          <strong>{item.name}</strong>
          <span>{item.role}</span>
        </div>
        <em>{item.status}</em>
      </div>
      <div className="ops-load-meter" aria-label={`${item.name} 负载 ${item.loadPercent}%`}>
        <i style={{ width: `${item.loadPercent}%` }} />
      </div>
      <div className="ops-component-load-row">
        <b>{item.loadPercent}%</b>
        <span>{item.primary}</span>
      </div>
      <p>{item.secondary}</p>
      <small>{item.detail}</small>
      <time>{formatDateTime(item.updatedAt)}</time>
    </section>
  );
}

export default function OperationsPage() {
  const [data, setData] = useState<OpsData>({ health: null, hosts: [], runtimes: [], workers: [], events: [] });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const [hostsPage, runtimesPage, workersPage] = await Promise.all([
        adminService.listHosts({ page: 1, pageSize: 500, status: 'all' }),
        adminService.listRuntimes({ page: 1, pageSize: 500, status: 'all' }),
        adminService.listWorkers({ page: 1, pageSize: 500, status: 'all' }),
      ]);
      const health = await adminService.getHealth();
      const eventResults = await Promise.allSettled(runtimesPage.items.slice(0, 30).map((runtime) => adminService.listRuntimeEvents(runtime.id, 20)));
      const events = eventResults.flatMap((result) => (result.status === 'fulfilled' ? result.value : []));
      setData({ health, hosts: hostsPage.items, runtimes: runtimesPage.items, workers: workersPage.items, events });
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'AdminService 数据加载失败');
      setData({ health: null, hosts: [], runtimes: [], workers: [], events: [] });
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const derived = useMemo(() => {
    const runningHosts = data.hosts.filter((item) => item.status === 'running').length;
    const offlineHosts = data.hosts.filter((item) => item.status === 'offline').length;
    const runningRuntimes = data.runtimes.filter((item) => item.status === 'running').length;
    const failedRuntimes = data.runtimes.filter((item) => item.status === 'failed' || item.convergence_status === 'failed').length;
    const pendingRuntimes = data.runtimes.filter((item) => item.convergence_status === 'pending').length;
    const staleRuntimes = data.runtimes.filter((item) => item.stale).length;
    const redisDisconnected = data.runtimes.filter((item) => item.redis_connected === false).length;
    const activeSessions = data.runtimes.reduce((sum, item) => sum + item.active_sessions, 0);
    const totalSendBps = data.runtimes.reduce((sum, item) => sum + item.send_bps, 0);
    const totalRecvBps = data.runtimes.reduce((sum, item) => sum + item.recv_bps, 0);
    const totalMounts = data.runtimes.reduce((sum, item) => sum + item.mounts, 0);
    const totalSources = data.runtimes.reduce((sum, item) => sum + item.sources, 0);
    const totalClients = data.runtimes.reduce((sum, item) => sum + item.clients, 0);
    const actualWorkers = data.runtimes.reduce((sum, item) => sum + item.actual_worker_count, 0);
    const desiredWorkers = data.runtimes.reduce((sum, item) => sum + item.desired_worker_count, 0);
    const loopDelays = data.runtimes.map((item) => item.loop_delay_ms_p95).filter((value) => value > 0);
    const maxLoopDelay = Math.max(...loopDelays, 0);
    const avgCpu = average(data.hosts.map((item) => item.cpu_load).filter((value) => value > 0));
    const score = clampScore(100 - failedRuntimes * 22 - offlineHosts * 16 - staleRuntimes * 8 - pendingRuntimes * 6 - redisDisconnected * 5);
    const healthLabel = score >= 95 ? '健康' : score >= 80 ? '需关注' : '故障风险';
    const healthColor = score >= 95 ? '#16a34a' : score >= 80 ? '#f59e0b' : '#ef4444';
    const topRuntimes = [...data.runtimes]
      .sort((left, right) => right.recv_bps + right.send_bps - (left.recv_bps + left.send_bps) || right.active_sessions - left.active_sessions)
      .slice(0, 12);
    const topWorkers = [...data.workers].sort((left, right) => right.active_sessions - left.active_sessions).slice(0, 5);
    const runtimeById = new Map(data.runtimes.map((item) => [item.id, item]));
    const sortedEvents = [...data.events].sort((left, right) => new Date(right.created_at).getTime() - new Date(left.created_at).getTime());
    const warningEvents = sortedEvents.filter((item) => item.level === 'warning');
    const errorEvents = sortedEvents.filter((item) => item.level === 'error');
    const maxThroughput = Math.max(...topRuntimes.flatMap((item) => [item.send_bps, item.recv_bps]), 1);
    const latencyBuckets: Bucket[] = [
      { label: '0-10ms', value: loopDelays.filter((item) => item <= 10).length },
      { label: '10-50ms', value: loopDelays.filter((item) => item > 10 && item <= 50).length },
      { label: '50-100ms', value: loopDelays.filter((item) => item > 50 && item <= 100).length },
      { label: '100-250ms', value: loopDelays.filter((item) => item > 100 && item <= 250).length },
      { label: '250ms+', value: loopDelays.filter((item) => item > 250).length },
    ];
    const stateBuckets: Bucket[] = [
      { label: '运行中', value: runningRuntimes },
      { label: '待处理', value: pendingRuntimes },
      { label: '失败', value: failedRuntimes },
      { label: '指标过期', value: staleRuntimes },
      { label: '离线', value: data.runtimes.filter((item) => item.status === 'offline').length },
    ];
    const eventBuckets: Bucket[] = [
      { label: '信息', value: sortedEvents.filter((item) => item.level === 'info').length },
      { label: '警告', value: warningEvents.length },
      { label: '错误', value: errorEvents.length },
    ];
    const adminLoad = clampPercent(18 + pendingRuntimes * 8 + failedRuntimes * 18 + staleRuntimes * 5 + activeSessions / 60);
    const postgresFailed = data.health?.postgres !== 'connected';
    const redisFailed = data.health?.redis !== 'connected';
    const platformComponents: ComponentLoad[] = [
      {
        id: 'admin',
        name: 'AdminService',
        role: '控制面服务',
        status: serviceStatusLabel(data.health?.status),
        tone: loadTone(adminLoad, data.health?.status !== 'ok'),
        loadPercent: adminLoad,
        primary: `${data.hosts.length} 台主机 / ${data.runtimes.length} 个运行时`,
        secondary: `${pendingRuntimes} 个待处理意图，${failedRuntimes} 个失败运行时`,
        detail: data.health?.control_plane?.repository ? `存储仓库：${data.health.control_plane.repository}` : '等待健康检查数据',
        updatedAt: data.health?.time ?? '',
      },
      {
        id: 'postgres',
        name: 'PostgreSQL',
        role: '业务数据源',
        status: serviceStatusLabel(data.health?.postgres),
        tone: loadTone(postgresFailed ? 100 : clampPercent(22 + data.runtimes.length * 2 + activeSessions / 90), postgresFailed),
        loadPercent: postgresFailed ? 100 : clampPercent(22 + data.runtimes.length * 2 + activeSessions / 90),
        primary: data.health?.postgres === 'connected' ? '连接正常' : '连接异常',
        secondary: `${data.health?.control_plane?.runtime_count ?? data.runtimes.length} 个运行时记录`,
        detail: '真实 CPU、连接数和慢查询需后端暴露数据库指标',
        updatedAt: data.health?.time ?? '',
      },
      {
        id: 'redis',
        name: 'Redis',
        role: '投影与通知总线',
        status: redisFailed || redisDisconnected > 0 ? '需关注' : serviceStatusLabel(data.health?.redis),
        tone: loadTone(redisFailed ? 100 : clampPercent(18 + redisDisconnected * 28 + totalSources * 3 + totalClients / 20), redisFailed),
        loadPercent: redisFailed ? 100 : clampPercent(18 + redisDisconnected * 28 + totalSources * 3 + totalClients / 20),
        primary: redisDisconnected > 0 ? `${redisDisconnected} 个运行时断开` : '运行时连接正常',
        secondary: `${totalSources} 个源站，${totalClients} 个客户端`,
        detail: '真实内存、键数量和命令 QPS 需后端暴露 Redis 指标',
        updatedAt: data.health?.time ?? '',
      },
    ];
    const agentComponents: ComponentLoad[] = data.hosts.map((host) => {
      const runtimeRows = data.runtimes.filter((runtime) => runtime.host_id === host.id);
      const cpu = host.cpu_load || 0;
      const memory = hostMemoryPercent(host);
      const loadPercent = clampPercent(Math.max(cpu, memory, runtimeRows.length * 14));
      return {
        id: host.id,
        name: host.name,
        role: 'Agent 容器',
        status: controlStatusLabel(host.status),
        tone: loadTone(loadPercent, host.status === 'offline' || host.status === 'failed'),
        loadPercent,
        primary: `CPU ${cpu ? formatPercent(cpu) : '-'} / 内存 ${memory ? formatPercent(memory) : '-'}`,
        secondary: `${runtimeRows.length} 个 Caster，${host.worker_count} 个工作线程`,
        detail: host.convergence_detail || '等待 Agent 心跳',
        updatedAt: host.last_heartbeat_at || host.last_metric_at,
      };
    });
    const casterComponents: ComponentLoad[] = data.runtimes.map((runtime) => {
      const throughputScore = (runtime.send_bps + runtime.recv_bps) / 50000;
      const sessionScore = runtime.active_sessions * 3;
      const loopScore = runtime.loop_delay_ms_p95 ? runtime.loop_delay_ms_p95 / 2 : 0;
      const loadPercent = clampPercent(Math.max(throughputScore, sessionScore, loopScore, runtime.actual_worker_count * 8));
      return {
        id: runtime.id,
        name: runtime.name,
        role: `Caster / ${runtime.host_name}`,
        status: controlStatusLabel(runtime.status),
        tone: loadTone(loadPercent, runtime.status === 'failed' || runtime.stale || runtime.redis_connected === false),
        loadPercent,
        primary: `${runtime.active_sessions} 会话 / ${formatBps(runtime.send_bps + runtime.recv_bps)}`,
        secondary: `${runtime.sources} 源站，${runtime.clients} 客户端，${runtime.actual_worker_count}/${runtime.desired_worker_count || runtime.actual_worker_count} 工作线程`,
        detail: runtime.redis_connected === false ? 'Redis 连接异常' : runtime.convergence_detail,
        updatedAt: runtime.last_metric_at || runtime.updated_at,
      };
    });

    return {
      runningHosts,
      offlineHosts,
      runningRuntimes,
      failedRuntimes,
      pendingRuntimes,
      staleRuntimes,
      redisDisconnected,
      activeSessions,
      totalSendBps,
      totalRecvBps,
      totalMounts,
      totalSources,
      totalClients,
      actualWorkers,
      desiredWorkers,
      maxLoopDelay,
      avgCpu,
      score,
      healthLabel,
      healthColor,
      topRuntimes,
      topWorkers,
      runtimeById,
      sortedEvents,
      warningEvents,
      errorEvents,
      maxThroughput,
      latencyBuckets,
      stateBuckets,
      eventBuckets,
      platformComponents,
      agentComponents,
      casterComponents,
    };
  }, [data]);

  const throughputLabels = derived.topRuntimes.map((item) => item.name);
  const throughputSeries: OpsSeries[] = [
    { name: '下行 bps', color: '#3b82f6', fill: true, values: derived.topRuntimes.map((item) => item.send_bps) },
    { name: '上行 bps', color: '#14b8a6', values: derived.topRuntimes.map((item) => item.recv_bps) },
  ];
  const healthDash = (derived.score / 100) * 314;
  const alertRows = [...derived.errorEvents, ...derived.warningEvents].slice(0, 8);
  const logRows = derived.sortedEvents.slice(0, 80);

  return (
    <div className="ops-page">
      {error ? <Alert type="error" showIcon message="AdminService 数据不可用" description={error} /> : null}
      <section className="ops-hero ops-panel">
        <div className="ops-hero-header">
          <div>
            <h2><NodeIndexOutlined /> NTRIP Caster 运维监控</h2>
            <p><span className="ops-live-dot" />实时读取 AdminService 控制面的主机、运行时、工作线程和运行时事件</p>
          </div>
          <div className="ops-hero-actions">
            <Button className="dashboard-filter-button">{data.hosts.length} 台主机</Button>
            <Button className="dashboard-filter-button">{data.runtimes.length} 个运行时</Button>
            <Button className="dashboard-filter-button">当前快照</Button>
            <Button className="dashboard-filter-button" icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
          </div>
        </div>

        <div className="ops-hero-grid">
          <div className="ops-health-card">
            <div className="ops-health-ring">
              <svg viewBox="0 0 132 132">
                <circle cx="66" cy="66" r="50" fill="none" stroke="#edf2f7" strokeWidth="14" />
                <circle cx="66" cy="66" r="50" fill="none" stroke={derived.healthColor} strokeDasharray={`${healthDash} 314`} strokeLinecap="round" strokeWidth="14" />
              </svg>
              <strong style={{ color: derived.healthColor }}>{derived.score}</strong>
              <span>运行评分</span>
            </div>
            <p>链路状态: <b style={{ color: derived.healthColor }}>{derived.healthLabel}</b></p>
          </div>

          <div className="ops-realtime-card">
            <div className="ops-panel-header">
              <h3>实时播发</h3>
              <span>实际快照</span>
            </div>
            <div className="ops-realtime-values">
              <div><span>客户端连接</span><strong>{formatNumber(derived.activeSessions)}</strong></div>
              <div><span>NTRIP 下行</span><strong>{formatBps(derived.totalSendBps)}</strong></div>
              <div><span>源站上行</span><strong>{formatBps(derived.totalRecvBps)}</strong></div>
              <div><span>Loop p95 Max</span><strong>{derived.maxLoopDelay ? `${formatNumber(derived.maxLoopDelay)} ms` : '-'}</strong></div>
            </div>
            <MiniLine values={derived.topRuntimes.map((item) => item.active_sessions)} color="#94a3b8" />
          </div>

          <div className="ops-stat-grid">
            <OpsStat title="在线主机" value={`${derived.runningHosts} / ${data.hosts.length}`} tone={derived.offlineHosts ? 'orange' : 'blue'} icon={<ClusterOutlined />} detail={`${derived.offlineHosts} 台离线`} />
            <OpsStat title="收敛运行时" value={`${derived.runningRuntimes} / ${data.runtimes.length}`} tone={derived.failedRuntimes ? 'red' : 'green'} icon={<SafetyCertificateOutlined />} detail={`${derived.pendingRuntimes} 个待处理，${derived.failedRuntimes} 个失败`} />
            <OpsStat title="活跃会话" value={formatNumber(derived.activeSessions)} tone="green" icon={<ApiOutlined />} detail={`${derived.totalClients} 个客户端，${derived.totalSources} 个源站`} />
            <OpsStat title="挂载点" value={formatNumber(derived.totalMounts)} tone="blue" icon={<LinkOutlined />} detail={`${derived.totalSources} 个源站已上报`} />
            <OpsStat title="Loop p95 最大值" value={derived.maxLoopDelay ? `${formatNumber(derived.maxLoopDelay)}ms` : '-'} tone={derived.maxLoopDelay > 100 ? 'orange' : 'green'} icon={<FieldTimeOutlined />} detail={`${derived.staleRuntimes} 个指标快照过期`} />
            <OpsStat title="Redis 实际状态" value={derived.redisDisconnected ? `${derived.redisDisconnected} 个断开` : '已连接'} tone={derived.redisDisconnected ? 'red' : 'green'} icon={<DatabaseOutlined />} detail="来自运行时实际状态 redis_connected" />
          </div>
        </div>

        <div className="ops-resource-strip">
          <div><span>CPU</span><strong>{derived.avgCpu ? formatPercent(derived.avgCpu) : '-'}</strong><small>主机标签 cpu_usage_pct</small></div>
          <div><span>内存</span><strong>{memoryPercent(data.hosts) ? formatPercent(memoryPercent(data.hosts)) : '-'}</strong><small>主机标签 memory_used/total</small></div>
          <div><span>工作线程分片</span><strong>{derived.actualWorkers} / {derived.desiredWorkers || derived.actualWorkers}</strong><small>实际 / 期望</small></div>
          <div><span>Redis</span><strong>{derived.redisDisconnected ? '异常' : '正常'}</strong><small>{derived.redisDisconnected} 个运行时断开</small></div>
          <div><span>NTRIP 会话</span><strong>{formatNumber(derived.activeSessions)}</strong><small>{derived.totalClients} 个客户端已上报</small></div>
          <div><span>数据链路</span><strong>{formatBps(derived.totalSendBps + derived.totalRecvBps)}</strong><small>下行 + 上行 bps</small></div>
        </div>
      </section>

      <section className="ops-panel ops-components-panel">
        <div className="ops-panel-header">
          <div>
            <h3><DatabaseOutlined /> 核心组件负载</h3>
            <p>聚合 AdminService 健康检查、控制面状态和运行时上报指标。</p>
          </div>
          <span>admin / PostgreSQL / Redis</span>
        </div>
        <div className="ops-component-grid ops-platform-grid">
          {derived.platformComponents.map((item) => <ComponentLoadCard key={item.id} item={item} />)}
        </div>
      </section>

      <section className="ops-component-detail-grid">
        <div className="ops-panel">
          <div className="ops-panel-header">
            <div>
              <h3><CloudServerOutlined /> Agent 负载详情</h3>
              <p>每个 Agent 容器的心跳、CPU、内存、运行时数量和工作线程数量。</p>
            </div>
            <span>{derived.agentComponents.length} 个 Agent</span>
          </div>
          {derived.agentComponents.length > 0 ? (
            <div className="ops-component-list">
              {derived.agentComponents.map((item) => <ComponentLoadCard key={item.id} item={item} />)}
            </div>
          ) : (
            <Empty description="AdminService 当前没有 Agent 心跳" />
          )}
        </div>

        <div className="ops-panel">
          <div className="ops-panel-header">
            <div>
              <h3><ClusterOutlined /> Caster 负载详情</h3>
              <p>每个 Caster 的会话、源站、客户端、吞吐、Loop p95 和 Redis 连接状态。</p>
            </div>
            <span>{derived.casterComponents.length} 个 Caster</span>
          </div>
          {derived.casterComponents.length > 0 ? (
            <div className="ops-component-list">
              {derived.casterComponents.map((item) => <ComponentLoadCard key={item.id} item={item} />)}
            </div>
          ) : (
            <Empty description="AdminService 当前没有 Caster 实际指标" />
          )}
        </div>
      </section>

      <section className="ops-mid-grid">
        <div className="ops-panel">
          <div className="ops-panel-header">
            <h3><CloudServerOutlined /> 工作线程分片</h3>
            <span>共 {data.workers.length} 组</span>
          </div>
          {derived.topWorkers.length > 0 ? (
            <div className="ops-worker-list">
              {derived.topWorkers.map((row) => (
                <div className="ops-worker-row" key={row.id}>
                  <div>
                    <strong>{row.name}</strong>
                    <span>{row.runtime_id}</span>
                  </div>
                  <div>
                    <b>{row.assigned_mount_points} 个挂载点</b>
                    <span>{formatNumber(row.active_sessions)} 个客户端</span>
                  </div>
                  <em>{controlStatusLabel(row.status)}</em>
                </div>
              ))}
            </div>
          ) : (
            <Empty description="AdminService 当前没有工作线程指标" />
          )}
        </div>

        <div className="ops-panel">
          <div className="ops-panel-header">
            <h3><ThunderboltOutlined /> Loop delay p95</h3>
            <span>按运行时</span>
          </div>
          <MiniLine values={derived.topRuntimes.map((item) => item.loop_delay_ms_p95)} color="#14b8a6" height={156} />
        </div>

        <div className="ops-panel ops-throughput-panel">
          <div className="ops-panel-header">
            <h3><LinkOutlined /> NTRIP 吞吐快照</h3>
            <span>bps</span>
          </div>
          <ThroughputChart series={throughputSeries} labels={throughputLabels} max={derived.maxThroughput} />
        </div>
      </section>

      <section className="ops-lower-grid">
        <DistributionPanel title="Loop p95 分布" subtitle="运行时实际指标" buckets={derived.latencyBuckets} />
        <DistributionPanel title="运行时状态分布" subtitle="期望 / 实际收敛" buckets={derived.stateBuckets} />
        <DistributionPanel title="事件级别分布" subtitle="运行时事件" buckets={derived.eventBuckets} />
      </section>

      <section className="ops-panel ops-alert-panel">
        <div className="ops-panel-header">
          <div>
            <h3><ExclamationCircleOutlined /> 告警事件</h3>
            <p>来自 AdminService 运行时事件的警告和错误级别事件。</p>
          </div>
          <div className="ops-filter-row">
            <Button className="dashboard-filter-button">{alertRows.length} 条活动告警</Button>
            <Button className="dashboard-filter-button" icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
          </div>
        </div>
        {alertRows.length > 0 ? (
          <div className="ops-log-table">
            <table>
              <thead>
                <tr>
                  <th>时间</th>
                  <th>级别</th>
                  <th>运行时</th>
                  <th>事件</th>
                </tr>
              </thead>
              <tbody>
                {alertRows.map((row) => (
                  <tr key={row.id}>
                    <td>{formatDateTime(row.created_at)}</td>
                    <td><span className={`ops-log-level ops-log-${logLevelClass(row.level)}`}>{eventLevelLabel(row.level)}</span></td>
                    <td>{runtimeName(derived.runtimeById, row)}</td>
                    <td>{row.message}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <div className="ops-alert-empty">AdminService 当前没有警告或错误级别运行时事件</div>
        )}
      </section>

      <section className="ops-panel ops-log-panel">
        <div className="ops-panel-header">
          <div>
            <h3><FileSearchOutlined /> 系统日志</h3>
            <p>运行时事件查询结果，按发生时间倒序展示。</p>
          </div>
          <div className="ops-log-counters">
            <span>已载入 {logRows.length}</span>
            <span>运行时 {data.runtimes.length}</span>
            <b>警告 {derived.warningEvents.length}</b>
            <em>错误 {derived.errorEvents.length}</em>
          </div>
        </div>
        <div className="ops-log-actions">
          <Button icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
          <Button danger icon={<WarningOutlined />} disabled>清理过期日志</Button>
        </div>
        <div className="ops-log-table">
          <table>
            <thead>
              <tr>
                <th>时间</th>
                <th>级别</th>
                <th>模块</th>
                <th>日志内容</th>
              </tr>
            </thead>
            <tbody>
              {logRows.length > 0 ? (
                logRows.map((row) => (
                  <tr key={row.id}>
                    <td>{formatDateTime(row.created_at)}</td>
                    <td><span className={`ops-log-level ops-log-${logLevelClass(row.level)}`}>{eventLevelLabel(row.level)}</span></td>
                    <td>{row.type || runtimeName(derived.runtimeById, row)}</td>
                    <td>{row.message}</td>
                  </tr>
                ))
              ) : (
                <tr>
                  <td colSpan={4}>AdminService 当前没有返回运行时事件</td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      </section>
    </div>
  );
}
