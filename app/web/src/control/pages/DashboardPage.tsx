import {
  CalendarOutlined,
  ClockCircleOutlined,
  ClusterOutlined,
  DatabaseOutlined,
  InboxOutlined,
  ReloadOutlined,
  ThunderboltOutlined,
} from '@ant-design/icons';
import { Alert, Button, Empty } from 'antd';
import type { ReactNode } from 'react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { ControlPlaneOverview, HostSummary, RuntimeSummary } from '../../api/contracts';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { formatDateTime } from './useControlPage';

type MetricTone = 'orange' | 'blue' | 'purple' | 'red';

type ChartSeries = {
  name: string;
  color: string;
  values: number[];
  dashed?: boolean;
};

type DashboardData = {
  overview: ControlPlaneOverview;
  hosts: HostSummary[];
  runtimes: RuntimeSummary[];
};

const emptyOverview: ControlPlaneOverview = {
  running_hosts: 0,
  offline_hosts: 0,
  running_runtimes: 0,
  failed_runtimes: 0,
  draining_workers: 0,
  active_sessions: 0,
};

function DashboardMetric({
  label,
  value,
  detail,
  icon,
  tone,
}: {
  label: string;
  value: ReactNode;
  detail: ReactNode;
  icon: ReactNode;
  tone: MetricTone;
}) {
  return (
    <section className={`dashboard-metric dashboard-metric-${tone}`}>
      <div className="dashboard-metric-icon">{icon}</div>
      <div className="dashboard-metric-copy">
        <span>{label}</span>
        <strong>{value}</strong>
        <div>{detail}</div>
      </div>
    </section>
  );
}

function xAt(index: number, total: number, left: number, width: number) {
  if (total <= 1) return left + width / 2;
  return left + (index * width) / (total - 1);
}

function seriesPoints(values: number[], max: number, width: number, height: number, left: number, top: number) {
  const safeMax = Math.max(max, 1);
  return values
    .map((value, index) => {
      const x = xAt(index, values.length, left, width);
      const y = top + height - (Math.min(value, safeMax) / safeMax) * height;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');
}

function DashboardLineChart({
  series,
  labels,
  max,
  yLabels,
  compact = false,
}: {
  series: ChartSeries[];
  labels: string[];
  max: number;
  yLabels: string[];
  compact?: boolean;
}) {
  const hasRows = labels.length > 0 && series.some((item) => item.values.some((value) => value > 0));
  if (!hasRows) {
    return (
      <div className="dashboard-chart">
        <Empty description="AdminService 当前没有返回可绘制的指标" />
      </div>
    );
  }

  const left = 62;
  const top = 18;
  const width = 650;
  const height = compact ? 144 : 182;
  const viewHeight = compact ? 222 : 272;
  const skip = labels.length > 8 ? Math.ceil(labels.length / 8) : 1;
  const gridLines = yLabels.map((label, index) => {
    const y = top + (index * height) / (yLabels.length - 1);
    return { label, y };
  });

  return (
    <div className="dashboard-chart">
      <div className="dashboard-chart-legend">
        {series.map((item) => (
          <span key={item.name}>
            <i style={{ borderColor: item.color }} />
            {item.name}
          </span>
        ))}
      </div>
      <svg viewBox={`0 0 760 ${viewHeight}`} role="img" aria-label="dashboard live metrics chart">
        {gridLines.map((line) => (
          <g key={line.label}>
            <line className="dashboard-chart-grid" x1={left} x2={left + width} y1={line.y} y2={line.y} />
            <text className="dashboard-chart-label" x={left - 10} y={line.y + 4} textAnchor="end">
              {line.label}
            </text>
          </g>
        ))}
        {labels.map((label, index) => {
          const x = xAt(index, labels.length, left, width);
          return (
            <g key={`${label}-${index}`}>
              <line className="dashboard-chart-grid" x1={x} x2={x} y1={top} y2={top + height} />
              {index % skip === 0 ? (
                <text className="dashboard-chart-label dashboard-chart-xlabel" x={x - 2} y={top + height + 36} textAnchor="end">
                  {label}
                </text>
              ) : null}
            </g>
          );
        })}
        {series.map((item) => (
          <polyline
            key={item.name}
            fill="none"
            stroke={item.color}
            strokeDasharray={item.dashed ? '7 7' : undefined}
            strokeLinecap="round"
            strokeLinejoin="round"
            strokeWidth={3}
            points={seriesPoints(item.values, max, width, height, left, top)}
          />
        ))}
        {series.map((item) =>
          item.values.map((value, index) => {
            const x = xAt(index, item.values.length, left, width);
            const y = top + height - (Math.min(value, Math.max(max, 1)) / Math.max(max, 1)) * height;
            return <circle key={`${item.name}-${index}`} cx={x} cy={y} r="2.7" fill="#ffffff" stroke={item.color} strokeWidth="2" />;
          }),
        )}
      </svg>
    </div>
  );
}

function RuntimeStatusDonut({ runtimes }: { runtimes: RuntimeSummary[] }) {
  const total = runtimes.length;
  const segments = [
    { key: 'running', label: 'Running', color: '#22c55e', count: runtimes.filter((item) => item.status === 'running').length },
    { key: 'pending', label: 'Pending', color: '#f59e0b', count: runtimes.filter((item) => item.status === 'pending' || item.convergence_status === 'pending').length },
    { key: 'failed', label: 'Failed', color: '#ef4444', count: runtimes.filter((item) => item.status === 'failed' || item.convergence_status === 'failed').length },
    { key: 'stopped', label: 'Stopped', color: '#94a3b8', count: runtimes.filter((item) => item.status === 'stopped' || item.status === 'offline' || item.status === 'unknown').length },
  ].filter((item) => item.count > 0);
  const circumference = 389.6;
  let offset = 0;

  return (
    <div className="dashboard-donut" aria-label="Runtime 状态分布圆环图">
      <svg viewBox="0 0 180 180">
        <circle cx="90" cy="90" r="62" fill="none" stroke="#e8eef5" strokeWidth="34" />
        {segments.map((segment) => {
          const dash = total > 0 ? (segment.count / total) * circumference : 0;
          const dashOffset = -offset;
          offset += dash;
          return (
            <circle
              key={segment.key}
              cx="90"
              cy="90"
              r="62"
              fill="none"
              stroke={segment.color}
              strokeDasharray={`${dash} ${circumference - dash}`}
              strokeDashoffset={dashOffset}
              strokeLinecap="butt"
              strokeWidth="34"
            />
          );
        })}
      </svg>
      <div className="dashboard-donut-center">
        <strong>{total}</strong>
        <span>Runtime</span>
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

function scaleLabels(max: number, suffix = '') {
  const safeMax = Math.max(max, 1);
  return [1, 0.75, 0.5, 0.25, 0].map((ratio) => `${Math.round(safeMax * ratio).toLocaleString('zh-CN')}${suffix}`);
}

function memoryPercent(host: HostSummary) {
  if (!host.memory_total_gb) return 0;
  return Math.min(100, (host.memory_used_gb / host.memory_total_gb) * 100);
}

export default function DashboardPage() {
  const [data, setData] = useState<DashboardData>({ overview: emptyOverview, hosts: [], runtimes: [] });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const [overview, hostsPage, runtimesPage] = await Promise.all([
        adminService.getOverview(),
        adminService.listHosts({ page: 1, pageSize: 500, status: 'all' }),
        adminService.listRuntimes({ page: 1, pageSize: 500, status: 'all' }),
      ]);
      setData({ overview, hosts: hostsPage.items, runtimes: runtimesPage.items });
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'AdminService 数据加载失败');
      setData({ overview: emptyOverview, hosts: [], runtimes: [] });
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const derived = useMemo(() => {
    const totalSendBps = data.runtimes.reduce((sum, item) => sum + item.send_bps, 0);
    const totalRecvBps = data.runtimes.reduce((sum, item) => sum + item.recv_bps, 0);
    const desiredWorkers = data.runtimes.reduce((sum, item) => sum + item.desired_worker_count, 0);
    const actualWorkers = data.runtimes.reduce((sum, item) => sum + item.actual_worker_count, 0);
    const pendingRuntimes = data.runtimes.filter((item) => item.convergence_status === 'pending').length;
    const failedRuntimes = data.runtimes.filter((item) => item.convergence_status === 'failed' || item.status === 'failed').length;
    const staleRuntimes = data.runtimes.filter((item) => item.stale).length;
    const topRuntimes = [...data.runtimes]
      .sort((left, right) => right.active_sessions - left.active_sessions || right.recv_bps + right.send_bps - (left.recv_bps + left.send_bps))
      .slice(0, 12);
    const hostRows = [...data.hosts].sort((left, right) => right.runtime_count - left.runtime_count || left.name.localeCompare(right.name)).slice(0, 12);
    const maxThroughput = Math.max(...topRuntimes.flatMap((item) => [item.send_bps, item.recv_bps]), 1);

    return {
      totalSendBps,
      totalRecvBps,
      desiredWorkers,
      actualWorkers,
      pendingRuntimes,
      failedRuntimes,
      staleRuntimes,
      topRuntimes,
      hostRows,
      maxThroughput,
    };
  }, [data]);

  const runtimeLabels = derived.topRuntimes.map((item) => item.name);
  const throughputSeries: ChartSeries[] = [
    { name: 'Send bps', color: '#3b82f6', values: derived.topRuntimes.map((item) => item.send_bps) },
    { name: 'Recv bps', color: '#14b8a6', values: derived.topRuntimes.map((item) => item.recv_bps) },
    { name: 'Active sessions', color: '#f59e0b', values: derived.topRuntimes.map((item) => item.active_sessions), dashed: true },
  ];
  const hostLabels = derived.hostRows.map((item) => item.name);
  const hostSeries: ChartSeries[] = [
    { name: 'CPU %', color: '#3b82f6', values: derived.hostRows.map((item) => item.cpu_load) },
    { name: 'Memory %', color: '#14b8a6', values: derived.hostRows.map(memoryPercent) },
  ];
  const visibleRuntimes = derived.topRuntimes.slice(0, 8);

  return (
    <div className="dashboard-page">
      {error ? <Alert type="error" showIcon message="AdminService 数据不可用" description={error} /> : null}
      <section className="dashboard-metric-grid">
        <DashboardMetric
          label="在线主机"
          value={loading ? '-' : `${data.overview.running_hosts} / ${data.hosts.length}`}
          tone="orange"
          icon={<InboxOutlined />}
          detail={<><em className="dashboard-money-orange">{data.overview.offline_hosts}</em><span> offline</span></>}
        />
        <DashboardMetric
          label="运行 Runtime"
          value={loading ? '-' : `${data.overview.running_runtimes} / ${data.runtimes.length}`}
          tone="blue"
          icon={<DatabaseOutlined />}
          detail={<><em className={derived.failedRuntimes > 0 ? 'dashboard-money-orange' : 'dashboard-money-green'}>{derived.failedRuntimes}</em><span> failed, {derived.pendingRuntimes} pending</span></>}
        />
        <DashboardMetric
          label="活跃会话"
          value={loading ? '-' : formatNumber(data.overview.active_sessions)}
          tone="purple"
          icon={<ThunderboltOutlined />}
          detail={<span>{formatBps(derived.totalRecvBps)} recv / {formatBps(derived.totalSendBps)} send</span>}
        />
        <DashboardMetric
          label="Worker 实际/目标"
          value={loading ? '-' : `${formatNumber(derived.actualWorkers)} / ${formatNumber(derived.desiredWorkers || derived.actualWorkers)}`}
          tone="red"
          icon={<ClockCircleOutlined />}
          detail={<span>{derived.staleRuntimes} stale metric snapshots</span>}
        />
      </section>

      <section className="dashboard-toolbar">
        <div className="dashboard-toolbar-group">
          <span>数据来源:</span>
          <Button className="dashboard-filter-button" icon={<ClusterOutlined />}>
            AdminService live control API
          </Button>
          <Button className="dashboard-filter-button" icon={<CalendarOutlined />}>
            当前快照
          </Button>
        </div>
        <div className="dashboard-toolbar-group">
          <Button className="dashboard-filter-button" icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
        </div>
      </section>

      <section className="dashboard-main-grid">
        <div className="dashboard-panel dashboard-model-panel">
          <div className="dashboard-panel-header">
            <h3>Runtime 状态分布</h3>
            <div className="dashboard-segmented">
              <button className="active" type="button">状态</button>
              <button type="button">会话</button>
            </div>
          </div>
          <div className="dashboard-model-content">
            <RuntimeStatusDonut runtimes={data.runtimes} />
            {visibleRuntimes.length > 0 ? (
              <table className="dashboard-model-table">
                <thead>
                  <tr>
                    <th>Runtime</th>
                    <th>状态</th>
                    <th>会话</th>
                    <th>Worker</th>
                    <th>Recv</th>
                    <th>指标</th>
                  </tr>
                </thead>
                <tbody>
                  {visibleRuntimes.map((row) => (
                    <tr key={row.id}>
                      <td><span className="dashboard-model-name">› {row.name}</span></td>
                      <td><StatusBadge status={row.status} /></td>
                      <td>{formatNumber(row.active_sessions)}</td>
                      <td>{row.actual_worker_count} / {row.desired_worker_count}</td>
                      <td>{formatBps(row.recv_bps)}</td>
                      <td>{formatDateTime(row.last_metric_at)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            ) : (
              <Empty description="AdminService 当前没有 Runtime" />
            )}
          </div>
        </div>

        <div className="dashboard-panel">
          <div className="dashboard-panel-header">
            <h3>Runtime 当前吞吐</h3>
          </div>
          <DashboardLineChart
            compact
            max={derived.maxThroughput}
            series={throughputSeries}
            labels={runtimeLabels}
            yLabels={scaleLabels(derived.maxThroughput)}
          />
        </div>
      </section>

      <section className="dashboard-panel dashboard-wide-panel">
        <div className="dashboard-panel-header">
          <h3>Host 资源快照</h3>
        </div>
        <DashboardLineChart
          max={100}
          series={hostSeries}
          labels={hostLabels}
          yLabels={scaleLabels(100, '%')}
        />
      </section>

      <section className="dashboard-panel dashboard-wide-panel">
        <div className="dashboard-panel-header">
          <h3>收敛状态</h3>
        </div>
        {data.runtimes.length > 0 ? (
          <table className="dashboard-model-table">
            <thead>
              <tr>
                <th>Runtime</th>
                <th>Host</th>
                <th>Desired</th>
                <th>Actual</th>
                <th>Convergence</th>
                <th>Config</th>
                <th>Updated</th>
              </tr>
            </thead>
            <tbody>
              {data.runtimes.slice(0, 12).map((row) => (
                <tr key={row.id}>
                  <td><span className="dashboard-model-name">› {row.name}</span></td>
                  <td>{row.host_name}</td>
                  <td>{row.desired_state}</td>
                  <td>{row.actual_state || '-'}</td>
                  <td><ConvergenceBadge status={row.convergence_status} /></td>
                  <td>{row.current_config_version_id} / {row.target_config_version_id}</td>
                  <td>{formatDateTime(row.updated_at)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : (
          <Empty description="AdminService 当前没有返回 Runtime desired/actual 状态" />
        )}
      </section>
    </div>
  );
}
