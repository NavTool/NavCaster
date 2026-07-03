import {
  ApiOutlined,
  CloudServerOutlined,
  ClusterOutlined,
  DatabaseOutlined,
  DownOutlined,
  ExclamationCircleOutlined,
  FieldTimeOutlined,
  FileSearchOutlined,
  LinkOutlined,
  NodeIndexOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  SettingOutlined,
  ThunderboltOutlined,
  WarningOutlined,
} from '@ant-design/icons';
import { Button } from 'antd';
import type { ReactNode } from 'react';

type OpsTone = 'green' | 'blue' | 'orange' | 'red';

type OpsSeries = {
  name: string;
  color: string;
  values: number[];
  fill?: boolean;
};

const sparkValues = [10, 12, 14, 13, 15, 14, 12, 11, 13, 18, 22, 20, 16, 15, 14, 13, 11, 10, 12, 15, 14, 13, 12, 11];

const latencySwitch = [0.002, 0.002, 0.002, 0.002, 0.002, 0.002, 0.002, 0.036, 0.002, 0.002, 0.002, 0.002];

const throughputSeries: OpsSeries[] = [
  { name: '上行源站', color: '#3b82f6', fill: true, values: [3, 11, 2, 8, 17, 21, 6, 18, 20, 12, 24, 16, 19, 28, 23, 33, 12, 26, 21, 35, 17, 31, 36, 8] },
  { name: '下行播发', color: '#14b8a6', values: [2, 5, 1, 6, 12, 14, 5, 15, 17, 10, 18, 13, 16, 24, 21, 28, 10, 20, 17, 30, 14, 25, 29, 7] },
];

const requestBuckets = [
  { label: '0-50ms', value: 18 },
  { label: '50-100ms', value: 44 },
  { label: '100-200ms', value: 78 },
  { label: '200-500ms', value: 122 },
  { label: '500-1000ms', value: 64 },
  { label: '1000ms+', value: 12 },
];

const workerRows = [
  { name: 'caster-edge-shanghai-a', shard: 'worker 0-63', mounts: '64 / 64', sessions: '1,284', state: '正常' },
  { name: 'caster-edge-shanghai-b', shard: 'worker 64-127', mounts: '63 / 64', sessions: '1,176', state: '轻微抖动' },
  { name: 'relay-pull-beijing-a', shard: 'pull relay', mounts: '28 / 30', sessions: '318', state: '正常' },
];

const logRows = [
  { time: '2026/7/3 11:04:03', level: 'info', module: 'caster.worker', message: 'mountpoint RTCM32_SE01 fanout completed clients=142 bytes=829145 latency_ms=18' },
  { time: '2026/7/3 11:04:01', level: 'info', module: 'ntrip.auth', message: 'client authorized account=survey-team-a mountpoint=RTCM32_SE01 remote=10.24.8.15' },
  { time: '2026/7/3 11:03:58', level: 'warn', module: 'relay.pull', message: 'source heartbeat jitter mountpoint=SHGNSS_03 p95_ms=448 threshold_ms=400' },
  { time: '2026/7/3 11:03:44', level: 'info', module: 'redis.projection', message: 'projection publish completed key=navcaster:runtime:edge-shanghai-a workers=64 sessions=1284' },
  { time: '2026/7/3 11:03:21', level: 'info', module: 'agent.supervisor', message: 'runtime health report host=edge-shanghai-01 cpu=5.5 mem=1.8 workers=64' },
  { time: '2026/7/3 11:02:57', level: 'info', module: 'caster.runtime', message: 'listener accepted ntrip client protocol=HTTP/1.1 path=/RTCM32_SE01 user_agent=NTRIP GNSS' },
  { time: '2026/7/3 11:02:20', level: 'info', module: 'caster.worker', message: 'slow disconnect sweep completed closed=0 idle_clients=3 runtime=edge-shanghai-a' },
  { time: '2026/7/3 11:01:49', level: 'info', module: 'admin.intent', message: 'desired worker count unchanged runtime=edge-shanghai-a workers=64 config=cfg-20260703-01' },
];

function pathFrom(values: number[], width: number, height: number, top = 4) {
  const max = Math.max(...values);
  const min = Math.min(...values);
  const range = max - min || 1;
  return values
    .map((value, index) => {
      const x = (index * width) / (values.length - 1);
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
  return (
    <svg className="ops-mini-line" viewBox={`0 0 ${width} ${height + 12}`} aria-hidden="true">
      <polyline fill="none" points={pathFrom(values, width, height)} stroke={color} strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" />
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
        <span>近1小时</span>
      </div>
      <div className="ops-empty-state">
        <FileSearchOutlined />
        <strong>暂无数据</strong>
        <span>{subtitle}</span>
      </div>
    </section>
  );
}

function ThroughputChart() {
  const width = 620;
  const height = 210;
  return (
    <div className="ops-chart">
      <div className="ops-chart-legend">
        {throughputSeries.map((series) => (
          <span key={series.name}><i style={{ borderColor: series.color }} />{series.name}</span>
        ))}
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} aria-label="NTRIP Caster throughput trend">
        {[0, 1, 2, 3, 4].map((line) => (
          <line className="ops-chart-grid" key={line} x1="42" x2="590" y1={24 + line * 38} y2={24 + line * 38} />
        ))}
        {throughputSeries.map((series) => {
          const points = series.values.map((value, index) => {
            const x = 42 + (index * 548) / (series.values.length - 1);
            const y = 176 - (value / 40) * 150;
            return `${x.toFixed(1)},${y.toFixed(1)}`;
          });
          return (
            <g key={series.name}>
              {series.fill ? <polygon fill={`${series.color}1f`} points={`42,176 ${points.join(' ')} 590,176`} /> : null}
              <polyline fill="none" stroke={series.color} strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" points={points.join(' ')} />
            </g>
          );
        })}
        {['10:04', '10:16', '10:28', '10:40', '10:52', '11:04'].map((label, index) => (
          <text className="ops-chart-label" key={label} x={42 + index * 110} y="202">{label}</text>
        ))}
      </svg>
    </div>
  );
}

export default function OperationsPage() {
  return (
    <div className="ops-page">
      <section className="ops-hero ops-panel">
        <div className="ops-hero-header">
          <div>
            <h2><NodeIndexOutlined /> NTRIP Caster 运维监控</h2>
            <p><span className="ops-live-dot" />实时监控 Agent、Caster Runtime、Worker 分片、挂载点和 NTRIP 数据链路</p>
          </div>
          <div className="ops-hero-actions">
            <Button className="dashboard-filter-button">全部节点 <DownOutlined /></Button>
            <Button className="dashboard-filter-button">全部挂载点 <DownOutlined /></Button>
            <Button className="dashboard-filter-button">近1小时 <DownOutlined /></Button>
            <Button className="dashboard-filter-button" icon={<ReloadOutlined />}>刷新</Button>
            <Button className="dashboard-filter-button" icon={<SettingOutlined />}>告警设置</Button>
          </div>
        </div>

        <div className="ops-hero-grid">
          <div className="ops-health-card">
            <div className="ops-health-ring">
              <svg viewBox="0 0 132 132">
                <circle cx="66" cy="66" r="50" fill="none" stroke="#edf2f7" strokeWidth="14" />
                <circle cx="66" cy="66" r="50" fill="none" stroke="#f59e0b" strokeDasharray="302 314" strokeLinecap="round" strokeWidth="14" />
              </svg>
              <strong>96</strong>
              <span>运行评分</span>
            </div>
            <p>链路状态: <b>轻微抖动</b></p>
          </div>

          <div className="ops-realtime-card">
            <div className="ops-panel-header">
              <h3>实时播发</h3>
              <span>10s 粒度</span>
            </div>
            <div className="ops-realtime-values">
              <div><span>客户端请求</span><strong>0.1 QPS</strong></div>
              <div><span>NTRIP 下行</span><strong>18,872.7 KB/s</strong></div>
              <div><span>源站上行</span><strong>15,019.0 KB/s</strong></div>
              <div><span>播发 TPS</span><strong>12,493.3</strong></div>
            </div>
            <MiniLine values={sparkValues} color="#94a3b8" />
          </div>

          <div className="ops-stat-grid">
            <OpsStat title="在线挂载点" value="186 / 192" tone="blue" icon={<ClusterOutlined />} detail="Relay Pull 42, Relay Push 18" />
            <OpsStat title="Caster SLA" value="99.997%" tone="green" icon={<SafetyCertificateOutlined />} detail="断流次数 0, 异常恢复 0" />
            <OpsStat title="客户端错误率" value="0.08%" tone="green" icon={<ApiOutlined />} detail="认证失败 2, 不存在挂载点 1" />
            <OpsStat title="源站心跳 P99" value="1,032ms" tone="orange" icon={<LinkOutlined />} detail="P95 468ms, Max 2,042ms" />
            <OpsStat title="首包延迟 P95" value="163ms" tone="red" icon={<FieldTimeOutlined />} detail="目标小于 120ms, 峰值 523ms" />
            <OpsStat title="Redis 投影延迟" value="6ms" tone="green" icon={<DatabaseOutlined />} detail="pub/sub 正常, projection 新鲜" />
          </div>
        </div>

        <div className="ops-resource-strip">
          <div><span>CPU</span><strong>5.5%</strong><small>峰值 18%, 平均 9%</small></div>
          <div><span>内存</span><strong>1.8%</strong><small>145 / 7,937 MB</small></div>
          <div><span>Worker 分片</span><strong>正常</strong><small>128 / 128 在线</small></div>
          <div><span>Redis</span><strong>正常</strong><small>投影 6ms, 队列 239</small></div>
          <div><span>NTRIP 鉴权</span><strong>正常</strong><small>认证缓存命中 98.6%</small></div>
          <div><span>数据链路</span><strong>正常</strong><small>源站、播发、回收稳定</small></div>
        </div>
      </section>

      <section className="ops-mid-grid">
        <div className="ops-panel">
          <div className="ops-panel-header">
            <h3><CloudServerOutlined /> Worker 分片</h3>
            <span>共 3 组</span>
          </div>
          <div className="ops-worker-list">
            {workerRows.map((row) => (
              <div className="ops-worker-row" key={row.name}>
                <div>
                  <strong>{row.name}</strong>
                  <span>{row.shard}</span>
                </div>
                <div>
                  <b>{row.mounts}</b>
                  <span>{row.sessions} clients</span>
                </div>
                <em>{row.state}</em>
              </div>
            ))}
          </div>
        </div>

        <div className="ops-panel">
          <div className="ops-panel-header">
            <h3><ThunderboltOutlined /> 平均播发延迟切换趋势</h3>
            <span>分片切换</span>
          </div>
          <MiniLine values={latencySwitch} color="#14b8a6" height={156} />
        </div>

        <div className="ops-panel ops-throughput-panel">
          <div className="ops-panel-header">
            <h3><LinkOutlined /> NTRIP 吞吐趋势</h3>
            <span>KB/s</span>
          </div>
          <ThroughputChart />
        </div>
      </section>

      <section className="ops-lower-grid">
        <div className="ops-panel">
          <div className="ops-panel-header">
            <h3>请求延迟分布</h3>
            <span>RTCM 包转发</span>
          </div>
          <div className="ops-bars">
            {requestBuckets.map((bucket) => (
              <div className="ops-bar" key={bucket.label}>
                <span>{bucket.label}</span>
                <div><i style={{ height: `${Math.max(8, bucket.value)}px` }} /></div>
              </div>
            ))}
          </div>
        </div>
        <EmptyOpsPanel title="异常分布" subtitle="该时间窗口内无断流、鉴权风暴或投影积压。" />
        <EmptyOpsPanel title="Relay 链路趋势" subtitle="Relay Pull / Push 在该时间窗口内无异常。" />
      </section>

      <section className="ops-panel ops-alert-panel">
        <div className="ops-panel-header">
          <div>
            <h3><ExclamationCircleOutlined /> 告警事件</h3>
            <p>最近的 Caster、Agent、Relay 和 Redis 投影告警。</p>
          </div>
          <div className="ops-filter-row">
            <Button className="dashboard-filter-button">近24小时 <DownOutlined /></Button>
            <Button className="dashboard-filter-button">全部级别 <DownOutlined /></Button>
            <Button className="dashboard-filter-button">全部节点 <DownOutlined /></Button>
            <Button className="dashboard-filter-button" icon={<ReloadOutlined />}>刷新</Button>
          </div>
        </div>
        <div className="ops-alert-empty">暂无活跃告警事件</div>
      </section>

      <section className="ops-panel ops-log-panel">
        <div className="ops-panel-header">
          <div>
            <h3><FileSearchOutlined /> 系统日志</h3>
            <p>NTRIP Caster、Agent Supervisor、Relay 和 Redis projection 的运行日志。</p>
          </div>
          <div className="ops-log-counters">
            <span>队列 0/5000</span>
            <span>已载入 4251</span>
            <b>已丢弃 0</b>
            <em>写入失败 0</em>
          </div>
        </div>
        <div className="ops-log-filters">
          <label>级别 <button type="button">info <DownOutlined /></button></label>
          <label>模块 <button type="button">caster.worker <DownOutlined /></button></label>
          <label>延迟阈值 <input value="100" readOnly /></label>
          <label>源站心跳阈值 <input value="400" readOnly /></label>
          <label>保留天数 <input value="30" readOnly /></label>
          <label>挂载点 <input placeholder="RTCM32_SE01" readOnly /></label>
          <label>client_id <input placeholder="client id" readOnly /></label>
          <label>account_id <input placeholder="account id" readOnly /></label>
        </div>
        <div className="ops-log-actions">
          <Button type="primary">搜索</Button>
          <Button>重置</Button>
          <Button danger icon={<WarningOutlined />}>清理过期日志</Button>
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
              {logRows.map((row) => (
                <tr key={`${row.time}-${row.module}-${row.message}`}>
                  <td>{row.time}</td>
                  <td><span className={`ops-log-level ops-log-${row.level}`}>{row.level}</span></td>
                  <td>{row.module}</td>
                  <td>{row.message}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>
    </div>
  );
}
