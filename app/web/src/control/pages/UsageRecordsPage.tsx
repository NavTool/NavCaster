import {
  CalendarOutlined,
  ClockCircleOutlined,
  DatabaseOutlined,
  DeleteOutlined,
  DollarCircleOutlined,
  DownOutlined,
  DownloadOutlined,
  IdcardOutlined,
  ReloadOutlined,
  SearchOutlined,
  SettingOutlined,
} from '@ant-design/icons';
import { Button } from 'antd';
import type { ReactNode } from 'react';

type UsageRow = {
  user: string;
  accessAccount: string;
  ownerAccount: string;
  mountPoint: string;
  direction: string;
  group: string;
  type: string;
  billing: string;
  traffic: string;
  duration: string;
  cost: string;
  firstByte: string;
  disconnects: string;
  time: string;
  ip: string;
};

type UsageSeries = {
  name: string;
  color: string;
  values: number[];
  dashed?: boolean;
};

const usageRows: UsageRow[] = [
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a01', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '66.9 MB', duration: '6.64s', cost: '$0.046507', firstByte: '3.96s', disconnects: '0', time: '2026/07/03 11:05:10', ip: '172.18.0.1' },
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a01', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '140.7 MB', duration: '44.43s', cost: '$0.760441', firstByte: '5.55s', disconnects: '0', time: '2026/07/03 11:05:00', ip: '172.18.0.1' },
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a02', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE02', direction: '下行 / NTRIP Client', group: 'CodeX', type: '测试', billing: '按量', traffic: '66.9 MB', duration: '10.87s', cost: '$0.045897', firstByte: '5.69s', disconnects: '0', time: '2026/07/03 11:04:59', ip: '172.18.0.1' },
  { user: 'vendor-relay@navcaster.local #2', accessAccount: 'relay-push-bj-01', ownerAccount: 'vendor-beijing', mountPoint: 'BJGNSS_01', direction: '上行 / Relay Push', group: '供应商', type: '正式', billing: '包月', traffic: '65.4 MB', duration: '8.52s', cost: '$0.042526', firstByte: '5.24s', disconnects: '0', time: '2026/07/03 11:04:47', ip: '172.18.0.2' },
  { user: 'survey-b@navcaster.local #1', accessAccount: 'rover-b03', ownerAccount: 'survey-team-b', mountPoint: 'SHGNSS_03', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '65.4 MB', duration: '14.25s', cost: '$0.055604', firstByte: '4.51s', disconnects: '0', time: '2026/07/03 11:04:36', ip: '172.18.0.1' },
  { user: 'survey-c@navcaster.local #1', accessAccount: 'rover-c01', ownerAccount: 'survey-team-c', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '试用', billing: '按量', traffic: '64.9 MB', duration: '21.00s', cost: '$0.064593', firstByte: '4.74s', disconnects: '1', time: '2026/07/03 11:04:18', ip: '172.18.0.1' },
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a01', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '230.8 MB', duration: '10.44s', cost: '$0.127397', firstByte: '10.19s', disconnects: '0', time: '2026/07/03 11:04:12', ip: '172.18.0.1' },
  { user: 'vendor-relay@navcaster.local #2', accessAccount: 'relay-pull-sh-02', ownerAccount: 'vendor-shanghai', mountPoint: 'SHGNSS_02', direction: '上行 / Relay Pull', group: '供应商', type: '正式', billing: '包月', traffic: '135.0 MB', duration: '14.82s', cost: '$0.556880', firstByte: '6.86s', disconnects: '0', time: '2026/07/03 11:04:01', ip: '172.18.0.3' },
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a02', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE02', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '64.9 MB', duration: '5.62s', cost: '$0.039998', firstByte: '3.46s', disconnects: '0', time: '2026/07/03 11:03:45', ip: '172.18.0.1' },
  { user: 'survey-b@navcaster.local #1', accessAccount: 'rover-b03', ownerAccount: 'survey-team-b', mountPoint: 'SHGNSS_03', direction: '下行 / NTRIP Client', group: 'CodeX', type: '测试', billing: '按量', traffic: '235.9 MB', duration: '12.63s', cost: '$0.131057', firstByte: '5.92s', disconnects: '0', time: '2026/07/03 11:03:45', ip: '172.18.0.1' },
  { user: 'survey-d@navcaster.local #1', accessAccount: 'rover-d07', ownerAccount: 'survey-team-d', mountPoint: 'RTCM32_SE04', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '59.8 MB', duration: '11.17s', cost: '$0.066938', firstByte: '5.72s', disconnects: '0', time: '2026/07/03 11:03:33', ip: '172.18.0.1' },
  { user: 'survey-c@navcaster.local #1', accessAccount: 'rover-c01', ownerAccount: 'survey-team-c', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '234.9 MB', duration: '12.98s', cost: '$0.131700', firstByte: '7.72s', disconnects: '0', time: '2026/07/03 11:03:32', ip: '172.18.0.1' },
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a01', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '59.3 MB', duration: '12.96s', cost: '$0.050547', firstByte: '3.81s', disconnects: '0', time: '2026/07/03 11:03:00', ip: '172.18.0.1' },
  { user: 'vendor-relay@navcaster.local #2', accessAccount: 'relay-push-bj-01', ownerAccount: 'vendor-beijing', mountPoint: 'BJGNSS_01', direction: '上行 / Relay Push', group: '供应商', type: '正式', billing: '包月', traffic: '234.4 MB', duration: '10.19s', cost: '$0.124694', firstByte: '7.40s', disconnects: '0', time: '2026/07/03 11:03:14', ip: '172.18.0.2' },
  { user: 'survey-e@navcaster.local #1', accessAccount: 'rover-e09', ownerAccount: 'survey-team-e', mountPoint: 'RTCM32_SE05', direction: '下行 / NTRIP Client', group: 'CodeX', type: '试用', billing: '按量', traffic: '58.8 MB', duration: '13.93s', cost: '$0.050731', firstByte: '3.52s', disconnects: '0', time: '2026/07/03 11:03:00', ip: '172.18.0.1' },
  { user: 'survey-a@navcaster.local #1', accessAccount: 'field-rover-a01', ownerAccount: 'survey-team-a', mountPoint: 'RTCM32_SE01', direction: '下行 / NTRIP Client', group: 'CodeX', type: '正式', billing: '按量', traffic: '231.8 MB', duration: '12.13s', cost: '$0.127649', firstByte: '7.82s', disconnects: '0', time: '2026/07/03 11:02:28', ip: '172.18.0.1' },
];

const trendSeries: UsageSeries[] = [
  { name: '下行流量', color: '#06b6d4', values: [14, 16, 25, 25, 24, 19, 28, 26, 31, 66, 4, 8, 2, 1, 1, 1, 3, 39, 40, 3] },
  { name: '上行流量', color: '#3b82f6', values: [2, 3, 5, 6, 8, 7, 6, 9, 8, 11, 4, 2, 1, 1, 1, 2, 2, 5, 4, 1] },
  { name: '连接数', color: '#8b5cf6', values: [80, 82, 81, 82, 81, 83, 82, 81, 84, 86, 78, 76, 74, 72, 76, 82, 85, 86, 87, 86], dashed: true },
];

const trendLabels = ['09:00', '10:00', '11:00', '12:00', '13:00', '14:00', '15:00', '16:00', '17:00', '18:00', '19:00', '20:00', '21:00', '22:00', '23:00', '00:00', '08:00', '09:00', '10:00', '11:00'];

function UsageMetric({
  label,
  value,
  detail,
  tone,
  icon,
}: {
  label: string;
  value: string;
  detail: string;
  tone: 'blue' | 'orange' | 'green' | 'purple';
  icon: ReactNode;
}) {
  return (
    <section className={`usage-metric usage-metric-${tone}`}>
      <div className="usage-metric-icon">{icon}</div>
      <div>
        <span>{label}</span>
        <strong>{value}</strong>
        <p>{detail}</p>
      </div>
    </section>
  );
}

function DonutCard({
  title,
  rows,
  segments = ['#3b82f6', '#22c55e'],
}: {
  title: string;
  rows: Array<{ name: string; sessions: string; traffic: string; cost: string }>;
  segments?: string[];
}) {
  return (
    <section className="usage-panel usage-donut-card">
      <div className="usage-panel-header">
        <h3>{title}</h3>
        <div className="usage-segmented"><button type="button">按流量</button><button type="button">按连接数</button></div>
      </div>
      <div className="usage-donut-content">
        <svg className="usage-donut" viewBox="0 0 180 180" aria-label={title}>
          <circle cx="90" cy="90" r="60" fill="none" stroke="#e8eef5" strokeWidth="34" />
          <circle cx="90" cy="90" r="60" fill="none" stroke={segments[0]} strokeDasharray="360 377" strokeDashoffset="96" strokeWidth="34" />
          <circle cx="90" cy="90" r="60" fill="none" stroke={segments[1]} strokeDasharray="8 377" strokeDashoffset="-260" strokeLinecap="round" strokeWidth="34" />
        </svg>
        <table className="usage-distribution-table">
          <thead>
            <tr>
              <th>维度</th>
              <th>连接</th>
              <th>流量</th>
              <th>费用</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.name}>
                <td><span>{row.name}</span></td>
                <td>{row.sessions}</td>
                <td>{row.traffic}</td>
                <td className="usage-money">{row.cost}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}

function UsageTrendChart() {
  const width = 720;
  const height = 245;
  const left = 52;
  const top = 22;
  const chartWidth = 610;
  const chartHeight = 168;
  return (
    <section className="usage-panel usage-trend-panel">
      <div className="usage-panel-header">
        <h3>流量使用趋势</h3>
        <div className="usage-chart-legend">
          {trendSeries.map((series) => <span key={series.name}><i style={{ borderColor: series.color }} />{series.name}</span>)}
        </div>
      </div>
      <div className="usage-chart">
        <svg viewBox={`0 0 ${width} ${height}`} aria-label="使用记录流量趋势">
          {[0, 1, 2, 3].map((line) => (
            <line className="usage-chart-grid-line" key={line} x1={left} x2={left + chartWidth} y1={top + line * 48} y2={top + line * 48} />
          ))}
          {trendLabels.map((label, index) => {
            const x = left + (index * chartWidth) / (trendLabels.length - 1);
            return index % 2 === 0 ? <text className="usage-chart-label usage-chart-xlabel" key={label} x={x - 2} y={top + chartHeight + 36} textAnchor="end">{label}</text> : null;
          })}
          {trendSeries.map((series) => {
            const points = series.values.map((value, index) => {
              const x = left + (index * chartWidth) / (series.values.length - 1);
              const y = top + chartHeight - (Math.min(value, 90) / 90) * chartHeight;
              return `${x.toFixed(1)},${y.toFixed(1)}`;
            });
            return (
              <polyline
                key={series.name}
                fill="none"
                points={points.join(' ')}
                stroke={series.color}
                strokeDasharray={series.dashed ? '7 7' : undefined}
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth={series.name === '下行流量' ? 3 : 2.4}
              />
            );
          })}
        </svg>
      </div>
    </section>
  );
}

function FilterField({ label, value }: { label: string; value: string }) {
  return (
    <label className="usage-filter-field">
      <span>{label}</span>
      <button type="button">{value} <DownOutlined /></button>
    </label>
  );
}

export default function UsageRecordsPage() {
  return (
    <div className="usage-page">
      <section className="usage-metric-grid">
        <UsageMetric label="总连接次数" value="3,136" detail="所有接入账号会话" tone="blue" icon={<IdcardOutlined />} />
        <UsageMetric label="总流量" value="422.29M" detail="下行 395.44M / 上行 26.85M" tone="orange" icon={<DatabaseOutlined />} />
        <UsageMetric label="总费用" value="$381.8899" detail="成本 $381.8899 / 标准 $381.8899" tone="green" icon={<DollarCircleOutlined />} />
        <UsageMetric label="平均会话时长" value="17.48s" detail="含客户端断开和源站切换" tone="purple" icon={<ClockCircleOutlined />} />
      </section>

      <section className="usage-toolbar">
        <div><span>时间范围:</span><Button className="dashboard-filter-button" icon={<CalendarOutlined />}>近24小时 <DownOutlined /></Button></div>
        <div><span>粒度:</span><Button className="dashboard-filter-button">按小时 <DownOutlined /></Button></div>
      </section>

      <section className="usage-chart-grid">
        <DonutCard
          title="接入账号分布"
          rows={[
            { name: 'field-rover-a01', sessions: '1,284', traffic: '186.26M', cost: '$178.96' },
            { name: 'relay-push-bj-01', sessions: '612', traffic: '92.10M', cost: '$81.59' },
            { name: 'rover-b03', sessions: '419', traffic: '84.77M', cost: '$61.31' },
            { name: 'relay-pull-sh-02', sessions: '211', traffic: '59.16M', cost: '$60.02' },
          ]}
        />
        <DonutCard
          title="用户分组用量分布"
          rows={[
            { name: 'CodeX', sessions: '2,594', traffic: '322.29M', cost: '$301.89' },
            { name: '供应商', sessions: '542', traffic: '100.00M', cost: '$80.00' },
          ]}
          segments={['#3b82f6', '#14b8a6']}
        />
        <DonutCard
          title="挂载点分布"
          rows={[
            { name: 'RTCM32_SE01', sessions: '1,413', traffic: '184.80M', cost: '$181.89' },
            { name: 'RTCM32_SE02', sessions: '724', traffic: '98.20M', cost: '$81.12' },
            { name: 'BJGNSS_01', sessions: '511', traffic: '74.70M', cost: '$68.31' },
          ]}
          segments={['#3b82f6', '#f59e0b']}
        />
        <UsageTrendChart />
      </section>

      <section className="usage-filter-panel">
        <div className="usage-filter-grid">
          <label className="usage-filter-input"><span>用户</span><input readOnly placeholder="按邮箱或用户 ID 搜索" /></label>
          <label className="usage-filter-input"><span>接入账号</span><input readOnly placeholder="按接入账号名搜索" /></label>
          <FilterField label="挂载点" value="请选择" />
          <label className="usage-filter-input"><span>账户</span><input readOnly placeholder="按账户或供应商搜索" /></label>
          <FilterField label="接入类型" value="请选择" />
          <FilterField label="计费类型" value="全部计费类型" />
          <FilterField label="计费模式" value="请选择" />
          <FilterField label="分组" value="请选择" />
        </div>
        <div className="usage-filter-actions">
          <Button icon={<ReloadOutlined />}>刷新</Button>
          <Button>重置</Button>
          <Button icon={<SettingOutlined />}>列设置</Button>
          <Button danger icon={<DeleteOutlined />}>清理</Button>
          <Button type="primary" icon={<DownloadOutlined />}>导出 Excel</Button>
        </div>
      </section>

      <section className="usage-table-panel">
        <div className="usage-tabs">
          <button className="active" type="button">用量明细</button>
          <button type="button">异常请求</button>
          <Button className="usage-search-button" icon={<SearchOutlined />}>搜索</Button>
        </div>
        <div className="usage-record-table">
          <table>
            <thead>
              <tr>
                <th>用户</th>
                <th>接入账号</th>
                <th>账户</th>
                <th>挂载点</th>
                <th>链路</th>
                <th>分组</th>
                <th>类型</th>
                <th>计费模式</th>
                <th>流量</th>
                <th>费用</th>
                <th>首包</th>
                <th>时长</th>
                <th>断流</th>
                <th>时间</th>
                <th>IP</th>
              </tr>
            </thead>
            <tbody>
              {usageRows.map((row) => (
                <tr key={`${row.user}-${row.accessAccount}-${row.time}`}>
                  <td><a href="#">{row.user}</a></td>
                  <td>{row.accessAccount}</td>
                  <td>{row.ownerAccount}</td>
                  <td><strong>{row.mountPoint}</strong></td>
                  <td><span>{row.direction}</span></td>
                  <td><em className="usage-tag usage-tag-purple">{row.group}</em></td>
                  <td><em className="usage-tag usage-tag-blue">{row.type}</em></td>
                  <td><em className="usage-tag usage-tag-slate">{row.billing}</em></td>
                  <td className="usage-traffic">{row.traffic}</td>
                  <td className="usage-money">{row.cost}</td>
                  <td>{row.firstByte}</td>
                  <td>{row.duration}</td>
                  <td>{row.disconnects}</td>
                  <td>{row.time}</td>
                  <td>{row.ip}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <div className="usage-pagination">
          <span>显示 1 至 {usageRows.length} 共 3136 条结果</span>
          <div><button type="button">1</button><button type="button">2</button><button type="button">3</button></div>
        </div>
      </section>
    </div>
  );
}
