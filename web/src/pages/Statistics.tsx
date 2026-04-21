import React, { useEffect, useState, useCallback } from 'react';
import { Card, Row, Col, Statistic, Table, Segmented, Spin, DatePicker, Button, Checkbox } from 'antd';
const { RangePicker } = DatePicker;
import {
  CloudServerOutlined, UserOutlined, SwapOutlined, ClockCircleOutlined,
} from '@ant-design/icons';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend,
} from 'recharts';
import dayjs from 'dayjs';
import type { Dayjs } from 'dayjs';
import {
  getStatsOverview, getMptRanking, getUsrRanking,
  type StatsOverview, type MptRankingItem, type UsrRankingItem,
} from '../api';

type RangeKey = 'today' | '7d' | '30d' | 'custom';
type DateRange = [Dayjs | null, Dayjs | null] | null;

function formatDuration(seconds: number): string {
  if (seconds < 60) return `${seconds}秒`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}分钟`;
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  return m > 0 ? `${h}小时${m}分` : `${h}小时`;
}

function formatHour(ts: number): string {
  const d = new Date(ts * 1000);
  return `${String(d.getHours()).padStart(2, '0')}:00`;
}

function formatDate(ts: number): string {
  const d = new Date(ts * 1000);
  return `${d.getMonth() + 1}/${d.getDate()} ${String(d.getHours()).padStart(2, '0')}:00`;
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1048576) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1073741824) return `${(bytes / 1048576).toFixed(1)} MB`;
  return `${(bytes / 1073741824).toFixed(2)} GB`;
}

const cardStyle = { borderColor: '#2e3450' };
const statCardStyle = { borderColor: '#2e3450', textAlign: 'center' as const };

const mptColumns = [
  { title: '排名', dataIndex: 'rank', key: 'rank', width: 60 },
  { title: '基站名称', dataIndex: 'name', key: 'name' },
  { title: '连接次数', dataIndex: 'connections', key: 'connections', sorter: (a: MptRankingItem & { rank: number }, b: MptRankingItem & { rank: number }) => a.connections - b.connections },
  { title: '总在线时长', dataIndex: 'total_duration', key: 'total_duration', render: (v: number) => formatDuration(v), sorter: (a: MptRankingItem & { rank: number }, b: MptRankingItem & { rank: number }) => a.total_duration - b.total_duration },
  { title: '最后活跃', dataIndex: 'last_seen', key: 'last_seen', render: (v: number) => v > 0 ? dayjs(v * 1000).format('MM-DD HH:mm') : '-' },
];

const usrColumns = [
  { title: '排名', dataIndex: 'rank', key: 'rank', width: 60 },
  { title: '用户名', dataIndex: 'name', key: 'name' },
  { title: '连接次数', dataIndex: 'connections', key: 'connections', sorter: (a: UsrRankingItem & { rank: number }, b: UsrRankingItem & { rank: number }) => a.connections - b.connections },
  { title: '总使用时长', dataIndex: 'total_duration', key: 'total_duration', render: (v: number) => formatDuration(v), sorter: (a: UsrRankingItem & { rank: number }, b: UsrRankingItem & { rank: number }) => a.total_duration - b.total_duration },
  { title: '使用基站数', dataIndex: 'mount_count', key: 'mount_count' },
  { title: '最后活跃', dataIndex: 'last_seen', key: 'last_seen', render: (v: number) => v > 0 ? dayjs(v * 1000).format('MM-DD HH:mm') : '-' },
];

const Statistics: React.FC = () => {
  const [range, setRange] = useState<RangeKey>('today');
  const [customRange, setCustomRange] = useState<DateRange>(null);
  // committedRange drives actual data fetching; custom range only commits on explicit query
  const [committedRange, setCommittedRange] = useState<{ key: RangeKey; custom: DateRange }>({ key: 'today', custom: null });
  const [overview, setOverview] = useState<StatsOverview | null>(null);
  const [mptRanking, setMptRanking] = useState<MptRankingItem[]>([]);
  const [usrRanking, setUsrRanking] = useState<UsrRankingItem[]>([]);
  const [loading, setLoading] = useState(false);
  const [visibleSeries, setVisibleSeries] = useState<Array<'mpt' | 'usr' | 'pull' | 'push'>>(['mpt', 'usr', 'pull', 'push']);

  const loadData = useCallback(async () => {
    setLoading(true);
    try {
      let params: { start?: number; end?: number; date?: string } = {};
      const now = Math.floor(Date.now() / 1000);
      const { key, custom } = committedRange;
      if (key === 'today') {
        // Use default (server returns today)
      } else if (key === '7d') {
        params = { start: now - 7 * 86400, end: now };
      } else if (key === '30d') {
        params = { start: now - 30 * 86400, end: now };
      } else if (key === 'custom' && custom?.[0] && custom?.[1]) {
        params = { start: custom[0].unix(), end: custom[1].unix() };
      }

      const overviewPromise = getStatsOverview(params);
      const rankingParams = { start: params.start, end: params.end, limit: 20 };

      const [ov, mpt, usr] = await Promise.all([
        overviewPromise,
        getMptRanking(rankingParams),
        getUsrRanking(rankingParams),
      ]);
      setOverview(ov);
      setMptRanking(mpt);
      setUsrRanking(usr);
    } catch (e) {
      console.error('Failed to load stats', e);
    } finally {
      setLoading(false);
    }
  }, [committedRange]);

  useEffect(() => { loadData(); }, [loadData]);

  // Preset ranges commit immediately; custom range waits for explicit query
  const handleRangeChange = (v: RangeKey) => {
    setRange(v);
    if (v !== 'custom') {
      setCommittedRange({ key: v, custom: null });
    }
  };

  const handleCustomQuery = () => {
    if (customRange?.[0] && customRange?.[1]) {
      setCommittedRange({ key: 'custom', custom: customRange });
    }
  };

  const trendData = overview?.hourly_trend?.map(h => ({
    ...h,
    label: ((overview.bucket_seconds ?? 3600) >= 86400) ? formatDate(h.ts) : formatHour(h.ts),
  })) ?? [];

  const mptDataSource = mptRanking.map((item, idx) => ({ ...item, rank: idx + 1, key: item.name }));
  const usrDataSource = usrRanking.map((item, idx) => ({ ...item, rank: idx + 1, key: item.name }));

  return (
    <div style={{ padding: 0 }}>
      <div style={{ marginBottom: 16, display: 'flex', alignItems: 'center', gap: 16 }}>
        <Segmented
          options={[
            { label: '今日', value: 'today' },
            { label: '近7天', value: '7d' },
            { label: '近30天', value: '30d' },
            { label: '自定义', value: 'custom' },
          ]}
          value={range}
          onChange={v => handleRangeChange(v as RangeKey)}
        />
        {range === 'custom' && (
          <>
            <RangePicker
              value={customRange as [Dayjs, Dayjs] | null}
              onChange={v => setCustomRange(v as DateRange)}
              showTime={{ format: 'HH:mm' }}
              format="MM-DD HH:mm"
              allowClear={false}
              style={{ width: 340 }}
            />
            <Button
              type="primary"
              onClick={handleCustomQuery}
              disabled={!customRange?.[0] || !customRange?.[1]}
            >
              查询
            </Button>
          </>
        )}
      </div>

      <Spin spinning={loading}>
        {/* Overview cards */}
        <Row gutter={[16, 16]}>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="基站连接数" value={overview?.mpt_connections ?? 0} prefix={<CloudServerOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="用户连接数" value={overview?.usr_connections ?? 0} prefix={<UserOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="Pull 连接数" value={overview?.pull_connections ?? 0} prefix={<SwapOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="Push 连接数" value={overview?.push_connections ?? 0} prefix={<SwapOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="峰值基站并发" value={overview?.peak_concurrent_mpt ?? 0} prefix={<SwapOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="峰值用户并发" value={overview?.peak_concurrent_usr ?? 0} prefix={<SwapOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="峰值 Pull 并发" value={overview?.peak_concurrent_pull ?? 0} prefix={<SwapOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="峰值 Push 并发" value={overview?.peak_concurrent_push ?? 0} prefix={<SwapOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="基站平均在线" value={formatDuration(overview?.avg_duration_mpt ?? 0)} prefix={<ClockCircleOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="用户平均使用" value={formatDuration(overview?.avg_duration_usr ?? 0)} prefix={<ClockCircleOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="独立基站数" value={overview?.unique_mountpoints ?? 0} prefix={<CloudServerOutlined />} />
            </Card>
          </Col>
          <Col xs={12} sm={6}>
            <Card style={statCardStyle} size="small">
              <Statistic title="独立用户数" value={overview?.unique_users ?? 0} prefix={<UserOutlined />} />
            </Card>
          </Col>
        </Row>

        {/* Hourly trend chart */}
        <Card
          title="连接数趋势"
          style={{ ...cardStyle, marginTop: 16 }}
          size="small"
          extra={
            <Checkbox.Group
              value={visibleSeries}
              onChange={(v) => setVisibleSeries(v as Array<'mpt' | 'usr' | 'pull' | 'push'>)}
              options={[
                { label: '基站', value: 'mpt' },
                { label: '用户', value: 'usr' },
                { label: 'Pull', value: 'pull' },
                { label: 'Push', value: 'push' },
              ]}
            />
          }
        >
          <ResponsiveContainer width="100%" height={280}>
            <LineChart data={trendData}>
              <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
              <XAxis dataKey="label" stroke="#6b7194" tick={{ fontSize: 11 }}
                interval={trendData.length > 48 ? Math.floor(trendData.length / 12) : undefined} />
              <YAxis stroke="#6b7194" tick={{ fontSize: 11 }} allowDecimals={false} />
              <Tooltip
                contentStyle={{ background: '#252a40', border: '1px solid #2e3450', borderRadius: 6 }}
                labelStyle={{ color: '#9da1b8' }}
              />
              <Legend />
              {visibleSeries.includes('mpt') && (
                <Line type="monotone" dataKey="mpt" name="基站" stroke="#4a8eff" strokeWidth={2} dot={false} activeDot={{ r: 4 }} />
              )}
              {visibleSeries.includes('usr') && (
                <Line type="monotone" dataKey="usr" name="用户" stroke="#52c41a" strokeWidth={2} dot={false} activeDot={{ r: 4 }} />
              )}
              {visibleSeries.includes('pull') && (
                <Line type="monotone" dataKey="pull" name="Pull" stroke="#faad14" strokeWidth={2} dot={false} activeDot={{ r: 4 }} />
              )}
              {visibleSeries.includes('push') && (
                <Line type="monotone" dataKey="push" name="Push" stroke="#eb2f96" strokeWidth={2} dot={false} activeDot={{ r: 4 }} />
              )}
            </LineChart>
          </ResponsiveContainer>
        </Card>

        {/* Rankings */}
        <Row gutter={16} style={{ marginTop: 16 }}>
          <Col xs={24} lg={12}>
            <Card title="基站在线时长排行" style={cardStyle} size="small">
              <Table
                dataSource={mptDataSource}
                columns={mptColumns}
                pagination={false}
                size="small"
                scroll={{ y: 360 }}
              />
            </Card>
          </Col>
          <Col xs={24} lg={12}>
            <Card title="用户使用时长排行" style={cardStyle} size="small">
              <Table
                dataSource={usrDataSource}
                columns={usrColumns}
                pagination={false}
                size="small"
                scroll={{ y: 360 }}
              />
            </Card>
          </Col>
        </Row>
      </Spin>
    </div>
  );
};

export default Statistics;
