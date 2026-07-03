import {
  CalendarOutlined,
  ClockCircleOutlined,
  DatabaseOutlined,
  DownOutlined,
  InboxOutlined,
  ReloadOutlined,
  ThunderboltOutlined,
} from '@ant-design/icons';
import { Button } from 'antd';
import type { ReactNode } from 'react';

type MetricTone = 'orange' | 'blue' | 'purple' | 'red';

type ChartSeries = {
  name: string;
  color: string;
  values: number[];
  dashed?: boolean;
};

const trendLabels = [
  '2026-07-02 09:00',
  '2026-07-02 10:00',
  '2026-07-02 11:00',
  '2026-07-02 12:00',
  '2026-07-02 13:00',
  '2026-07-02 14:00',
  '2026-07-02 15:00',
  '2026-07-02 16:00',
  '2026-07-02 17:00',
  '2026-07-02 18:00',
  '2026-07-02 19:00',
  '2026-07-02 20:00',
  '2026-07-02 21:00',
  '2026-07-02 22:00',
  '2026-07-02 23:00',
  '2026-07-03 00:00',
  '2026-07-03 01:00',
  '2026-07-03 08:00',
  '2026-07-03 09:00',
  '2026-07-03 10:00',
];

const tokenTrend: ChartSeries[] = [
  { name: 'Input', color: '#3b82f6', values: [0.8, 0.9, 1.2, 1.4, 1.6, 1.8, 2.2, 2.4, 3.1, 4.8, 0.5, 1.2, 0.7, 1.1, 0.8, 0.7, 0.9, 0.8, 3.8, 2.0] },
  { name: 'Output', color: '#34d399', values: [0.6, 0.7, 1.1, 1.0, 1.2, 1.1, 1.3, 1.2, 1.4, 2.0, 0.4, 0.9, 0.6, 0.7, 0.5, 0.5, 0.8, 0.6, 1.8, 1.1] },
  { name: 'Cache Creation', color: '#f59e0b', values: [0.2, 0.3, 0.5, 0.4, 0.6, 0.7, 0.5, 0.6, 0.9, 1.0, 0.4, 0.7, 0.5, 0.6, 0.4, 0.5, 0.7, 0.4, 0.8, 0.5] },
  { name: 'Cache Read', color: '#06b6d4', values: [14, 15, 26, 25, 24, 19, 29, 26, 31, 66, 3, 7, 1, 2, 0.8, 1, 1.4, 0.8, 38, 24] },
  { name: 'Cache Hit Rate', color: '#8b5cf6', values: [82, 83, 82, 81, 82, 81, 82, 82, 84, 86, 78, 79, 83, 62, 55, 68, 69, 63, 79, 82], dashed: true },
];

const recentUsage: ChartSeries[] = [
  { name: 'KORO', color: '#3b82f6', values: [18, 17, 18, 25, 29, 30, 27, 23, 32, 30, 33, 72, 3, 0.5, 1.4, 0.6, 0.9, 0.8, 41, 23] },
  { name: '草莓王', color: '#10b981', values: [0.4, 0.6, 3.8, 0.5, 2.1, 1.4, 0.6, 0.5, 1.1, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.5, 0.7, 0.5, 0.6, 3.0] },
  { name: 'LWM', color: '#f59e0b', values: [0.2, 0.4, 0.5, 0.3, 0.8, 0.7, 0.4, 0.5, 1.3, 1.2, 6.4, 0.6, 0.8, 2.1, 0.9, 1.0, 1.1, 0.7, 0.8, 1.4] },
];

const modelRows = [
  { model: 'gpt-5.5', requests: '2,913', token: '397.38M', actual: '$360.66', cost: '$360.66', standard: '$360.66' },
  { model: 'gpt-5.4', requests: '50', token: '2.10M', actual: '$1.59', cost: '$1.59', standard: '$1.59' },
  { model: 'codex-auto-review', requests: '15', token: '488.85K', actual: '$0.993', cost: '$0.993', standard: '$0.993' },
  { model: 'gpt-5.4-mini', requests: '11', token: '76.29K', actual: '$0.032', cost: '$0.032', standard: '$0.032' },
];

function DashboardMetric({
  label,
  value,
  detail,
  icon,
  tone,
}: {
  label: string;
  value: string;
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

function seriesPoints(values: number[], max: number, width: number, height: number, left: number, top: number) {
  return values
    .map((value, index) => {
      const x = left + (index * width) / (values.length - 1);
      const y = top + height - (Math.min(value, max) / max) * height;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');
}

function StaticLineChart({
  series,
  max,
  yLabels,
  rightLabels,
  compact = false,
}: {
  series: ChartSeries[];
  max: number;
  yLabels: string[];
  rightLabels?: string[];
  compact?: boolean;
}) {
  const left = 62;
  const top = 18;
  const width = 650;
  const height = compact ? 144 : 182;
  const viewHeight = compact ? 222 : 272;
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
      <svg viewBox={`0 0 760 ${viewHeight}`} role="img" aria-label="static dashboard line chart">
        {gridLines.map((line) => (
          <g key={line.label}>
            <line className="dashboard-chart-grid" x1={left} x2={left + width} y1={line.y} y2={line.y} />
            <text className="dashboard-chart-label" x={left - 10} y={line.y + 4} textAnchor="end">
              {line.label}
            </text>
          </g>
        ))}
        {trendLabels.map((label, index) => {
          const x = left + (index * width) / (trendLabels.length - 1);
          return (
            <g key={label}>
              <line className="dashboard-chart-grid" x1={x} x2={x} y1={top} y2={top + height} />
              {index % 2 === 0 ? (
                <text className="dashboard-chart-label dashboard-chart-xlabel" x={x - 2} y={top + height + 36} textAnchor="end">
                  {label}
                </text>
              ) : null}
            </g>
          );
        })}
        {rightLabels?.map((label, index) => {
          const y = top + (index * height) / (rightLabels.length - 1);
          return (
            <text className="dashboard-chart-right-label" key={label} x={left + width + 10} y={y + 4}>
              {label}
            </text>
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
            strokeWidth={item.name === 'Cache Read' || item.name === 'KORO' ? 3 : 2.2}
            points={seriesPoints(item.values, max, width, height, left, top)}
          />
        ))}
        {series.map((item) =>
          item.values.map((value, index) => {
            const x = left + (index * width) / (item.values.length - 1);
            const y = top + height - (Math.min(value, max) / max) * height;
            return <circle key={`${item.name}-${index}`} cx={x} cy={y} r="2.7" fill="#ffffff" stroke={item.color} strokeWidth="2" />;
          }),
        )}
      </svg>
    </div>
  );
}

export default function DashboardPage() {
  return (
    <div className="dashboard-page">
      <section className="dashboard-metric-grid">
        <DashboardMetric
          label="今日 Token"
          value="68.26M"
          tone="orange"
          icon={<InboxOutlined />}
          detail={<><em className="dashboard-money-green">$65.35</em><span> / </span><em className="dashboard-money-orange">$65.35</em><span> / $65.35</span></>}
        />
        <DashboardMetric
          label="总 Token"
          value="5.77B"
          tone="blue"
          icon={<DatabaseOutlined />}
          detail={<><em className="dashboard-money-green">$5.32K</em><span> / </span><em className="dashboard-money-orange">$5.32K</em><span> / $5.32K</span></>}
        />
        <DashboardMetric
          label="性能指标"
          value="8 RPM"
          tone="purple"
          icon={<ThunderboltOutlined />}
          detail={<><em className="dashboard-money-purple">46.58K</em><span> TPM</span></>}
        />
        <DashboardMetric
          label="平均响应"
          value="19.60s"
          tone="red"
          icon={<ClockCircleOutlined />}
          detail={<span>3 活跃用户</span>}
        />
      </section>

      <section className="dashboard-toolbar">
        <div className="dashboard-toolbar-group">
          <span>时间范围:</span>
          <Button className="dashboard-filter-button" icon={<CalendarOutlined />}>
            近24小时 <DownOutlined />
          </Button>
          <Button className="dashboard-filter-button" icon={<ReloadOutlined />}>刷新</Button>
        </div>
        <div className="dashboard-toolbar-group">
          <span>粒度:</span>
          <Button className="dashboard-filter-button">
            按小时 <DownOutlined />
          </Button>
        </div>
      </section>

      <section className="dashboard-main-grid">
        <div className="dashboard-panel dashboard-model-panel">
          <div className="dashboard-panel-header">
            <h3>模型分布</h3>
            <div className="dashboard-segmented">
              <button className="active" type="button">模型分布</button>
              <button type="button">用户消费榜</button>
            </div>
          </div>
          <div className="dashboard-model-content">
            <div className="dashboard-donut" aria-label="模型分布圆环图">
              <svg viewBox="0 0 180 180">
                <circle cx="90" cy="90" r="62" fill="none" stroke="#e8eef5" strokeWidth="34" />
                <circle cx="90" cy="90" r="62" fill="none" stroke="#3b82f6" strokeDasharray="382 390" strokeDashoffset="94" strokeLinecap="butt" strokeWidth="34" />
                <circle cx="90" cy="90" r="62" fill="none" stroke="#22c55e" strokeDasharray="4 390" strokeDashoffset="-288" strokeLinecap="round" strokeWidth="34" />
              </svg>
            </div>
            <table className="dashboard-model-table">
              <thead>
                <tr>
                  <th>模型</th>
                  <th>请求</th>
                  <th>Token</th>
                  <th>实际</th>
                  <th>成本</th>
                  <th>标准</th>
                </tr>
              </thead>
              <tbody>
                {modelRows.map((row) => (
                  <tr key={row.model}>
                    <td><span className="dashboard-model-name">› {row.model}</span></td>
                    <td>{row.requests}</td>
                    <td>{row.token}</td>
                    <td className="dashboard-money-green">{row.actual}</td>
                    <td className="dashboard-money-orange">{row.cost}</td>
                    <td>{row.standard}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>

        <div className="dashboard-panel">
          <div className="dashboard-panel-header">
            <h3>Token 使用趋势</h3>
          </div>
          <StaticLineChart
            compact
            max={100}
            series={tokenTrend}
            yLabels={['60.00M', '40.00M', '20.00M', '0']}
            rightLabels={['100%', '80%', '60%', '40%', '20%', '0%']}
          />
        </div>
      </section>

      <section className="dashboard-panel dashboard-wide-panel">
        <div className="dashboard-panel-header">
          <h3>最近使用 (Top 12)</h3>
        </div>
        <StaticLineChart
          max={80}
          series={recentUsage}
          yLabels={['80.00M', '70.00M', '60.00M', '50.00M', '40.00M', '30.00M', '20.00M', '10.00M', '0']}
        />
      </section>
    </div>
  );
}
