import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { Button, Card, Checkbox, Col, Empty, Row, Segmented, Select, Spin, Switch, Typography } from 'antd';
import {
  ArrowDownOutlined, ArrowUpOutlined, CloudServerOutlined, DashboardOutlined,
  HddOutlined, ReloadOutlined, SwapOutlined, UserOutlined,
} from '@ant-design/icons';
import {
  CartesianGrid, Legend, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis,
} from 'recharts';
import {
  getClusterMonitor, getNodeHistory, getRedisHistory,
  type ClusterMonitorInfo, type NodeHistorySnapshot,
} from '../api';
import type { RedisStatPoint } from '../api/types';
import { formatBytes, formatCpuPercent, formatMbps, formatSpeed, normalizeCpuPercent } from '../utils/format';

const { Title, Text } = Typography;

type Scope = 'cluster' | 'node';
type SeriesKey = 'mpt' | 'usr' | 'pull' | 'push';
type RedisRange = '1h' | '6h' | '24h' | '7d';

const TIME_RANGES = [
  { label: '1小时', duration: 3600, limit: 720, range: 'raw' as const, bucket: 5 },
  { label: '6小时', duration: 21600, limit: 4320, range: 'raw' as const, bucket: 30 },
  { label: '24小时', duration: 86400, limit: 17280, range: 'raw' as const, bucket: 120 },
  { label: '7天', duration: 604800, limit: 120960, range: 'raw' as const, bucket: 900 },
  { label: '30天', duration: 2592000, limit: 43200, range: '1m' as const, bucket: 3600 },
];

const REDIS_RANGES: Array<{ label: string; value: RedisRange; duration: number; bucket: number }> = [
  { label: '1小时', value: '1h', duration: 3600, bucket: 60 },
  { label: '6小时', value: '6h', duration: 21600, bucket: 300 },
  { label: '24小时', value: '24h', duration: 86400, bucket: 900 },
  { label: '7天', value: '7d', duration: 604800, bucket: 3600 },
];

interface ChartPoint {
  t: number;
  time: string;
  mpt: number;
  usr: number;
  pull: number;
  push: number;
  cpu: number;
  mem: number;
  recv_speed: number;
  send_speed: number;
}

interface RedisChartPoint {
  t: number;
  time: string;
  used_memory: number;
  used_memory_rss: number;
  used_memory_peak: number;
  mem_fragmentation_ratio: number;
  total_keys: number;
  ops_per_sec: number;
  hit_rate_pct: number;
  connected_clients: number;
  blocked_clients: number;
  input_kbps: number;
  output_kbps: number;
}

interface HistoryInput {
  history: NodeHistorySnapshot[];
}

interface RuntimeValues {
  cpu: number;
  mem: number;
  mpt: number;
  usr: number;
  pull: number;
  push: number;
  recv_speed: number;
  send_speed: number;
}

function formatTime(ts: number, longRange: boolean): string {
  const d = new Date(ts * 1000);
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  if (longRange) {
    return `${String(d.getMonth() + 1).padStart(2, '0')}/${String(d.getDate()).padStart(2, '0')} ${hh}:${mm}`;
  }
  return `${hh}:${mm}`;
}

function createTimeline(durationSeconds: number, bucketSeconds: number, longRange: boolean, end = Math.floor(Date.now() / 1000)) {
  const start = end - durationSeconds;
  const firstBucket = Math.ceil(start / bucketSeconds) * bucketSeconds;
  const lastBucket = Math.floor(end / bucketSeconds) * bucketSeconds;
  const times: Array<{ t: number; time: string }> = [];
  for (let t = firstBucket; t <= lastBucket; t += bucketSeconds) {
    times.push({ t, time: formatTime(t, longRange) });
  }
  return { start, end, times };
}

function resolveTimelineEnd(histories: HistoryInput[], bucketSeconds: number) {
  const now = Math.floor(Date.now() / 1000);
  const latest = histories.reduce((max, input) => (
    input.history.reduce((innerMax, snapshot) => Math.max(innerMax, snapshot.t || 0), max)
  ), 0);
  const staleAfter = Math.max(bucketSeconds * 3, 60);
  const end = latest > 0 && now - latest <= staleAfter ? latest : now;
  return Math.floor(end / bucketSeconds) * bucketSeconds;
}

function zeroValues(): RuntimeValues {
  return { cpu: 0, mem: 0, mpt: 0, usr: 0, pull: 0, push: 0, recv_speed: 0, send_speed: 0 };
}

function averageBucket(bucket: RuntimeValues & { samples: number }): RuntimeValues {
  const samples = Math.max(bucket.samples, 1);
  return {
    cpu: bucket.cpu / samples,
    mem: bucket.mem / samples,
    mpt: bucket.mpt / samples,
    usr: bucket.usr / samples,
    pull: bucket.pull / samples,
    push: bucket.push / samples,
    recv_speed: bucket.recv_speed / samples,
    send_speed: bucket.send_speed / samples,
  };
}

function aggregateHistory(
  histories: HistoryInput[],
  durationSeconds: number,
  bucketSeconds: number,
  longRange: boolean,
  timelineEnd = resolveTimelineEnd(histories, bucketSeconds),
): ChartPoint[] {
  const { start, end, times } = createTimeline(durationSeconds, bucketSeconds, longRange, timelineEnd);
  const buckets = new Map<number, RuntimeValues>();
  const shortGapSeconds = Math.max(bucketSeconds * 3, 60);

  histories.forEach(({ history }) => {
    const nodeBuckets = new Map<number, RuntimeValues & { samples: number }>();

    history.forEach((snapshot) => {
      if (snapshot.t < start || snapshot.t > end) return;
      const bucket = Math.floor(snapshot.t / bucketSeconds) * bucketSeconds;
      const current = nodeBuckets.get(bucket) || {
        samples: 0, cpu: 0, mem: 0, mpt: 0, usr: 0, pull: 0, push: 0, recv_speed: 0, send_speed: 0,
      };
      current.samples += 1;
      current.cpu += normalizeCpuPercent(snapshot.cpu || 0);
      current.mem += snapshot.mem || 0;
      current.mpt += snapshot.mpt || 0;
      current.usr += snapshot.usr || 0;
      current.pull += snapshot.pull || 0;
      current.push += snapshot.push || 0;
      current.recv_speed += snapshot.recv_speed || 0;
      current.send_speed += snapshot.send_speed || 0;
      nodeBuckets.set(bucket, current);
    });

    let lastSeen = 0;
    let lastValues: RuntimeValues | null = null;
    times.forEach(({ t }) => {
      const nodeBucket = nodeBuckets.get(t);
      let values: RuntimeValues;
      if (nodeBucket) {
        values = averageBucket(nodeBucket);
        lastSeen = t;
        lastValues = values;
      } else if (lastValues && t - lastSeen <= shortGapSeconds) {
        values = lastValues;
      } else {
        values = zeroValues();
      }

      const target = buckets.get(t) || zeroValues();
      target.cpu += values.cpu;
      target.mem += values.mem;
      target.mpt += values.mpt;
      target.usr += values.usr;
      target.pull += values.pull;
      target.push += values.push;
      target.recv_speed += values.recv_speed;
      target.send_speed += values.send_speed;
      buckets.set(t, target);
    });
  });

  const cpuDivisor = Math.max(histories.length, 1);
  return times.map(({ t, time }) => {
    const bucket = buckets.get(t);
    return {
      t,
      time,
      mpt: Math.round(bucket?.mpt || 0),
      usr: Math.round(bucket?.usr || 0),
      pull: Math.round(bucket?.pull || 0),
      push: Math.round(bucket?.push || 0),
      cpu: +(((bucket?.cpu || 0) / cpuDivisor).toFixed(1)),
      mem: bucket?.mem || 0,
      recv_speed: bucket?.recv_speed || 0,
      send_speed: bucket?.send_speed || 0,
    };
  });
}

function aggregateRedisHistory(points: RedisStatPoint[], durationSeconds: number, bucketSeconds: number, longRange: boolean): RedisChartPoint[] {
  const { start, end } = createTimeline(durationSeconds, bucketSeconds, longRange);
  const buckets = new Map<number, {
    samples: number; used_memory: number; used_memory_rss: number; used_memory_peak: number; mem_fragmentation_ratio: number;
    total_keys: number; ops_per_sec: number; hit_rate_pct: number; connected_clients: number; blocked_clients: number;
    input_kbps: number; output_kbps: number;
  }>();

  points.forEach((point) => {
    if (!point.t || point.used_memory <= 0 || point.t < start || point.t > end) return;
    const bucket = Math.floor(point.t / bucketSeconds) * bucketSeconds;
    const hits = point.hits || 0;
    const misses = point.misses || 0;
    const hitRate = typeof point.hit_rate === 'number'
      ? point.hit_rate * 100
      : (hits + misses > 0 ? hits * 100 / (hits + misses) : 0);
    const current = buckets.get(bucket) || {
      samples: 0, used_memory: 0, used_memory_rss: 0, used_memory_peak: 0, mem_fragmentation_ratio: 0,
      total_keys: 0, ops_per_sec: 0, hit_rate_pct: 0, connected_clients: 0, blocked_clients: 0,
      input_kbps: 0, output_kbps: 0,
    };
    current.samples += 1;
    current.used_memory += point.used_memory || 0;
    current.used_memory_rss += point.used_memory_rss || 0;
    current.used_memory_peak += point.used_memory_peak || 0;
    current.mem_fragmentation_ratio += point.mem_fragmentation_ratio || 0;
    current.total_keys += point.total_keys || 0;
    current.ops_per_sec += point.ops_per_sec || 0;
    current.hit_rate_pct += hitRate;
    current.connected_clients += point.connected_clients || 0;
    current.blocked_clients += point.blocked_clients || 0;
    current.input_kbps += point.input_kbps || 0;
    current.output_kbps += point.output_kbps || 0;
    buckets.set(bucket, current);
  });

  return Array.from(buckets.entries()).sort((a, b) => a[0] - b[0]).map(([t, bucket]) => {
    const samples = Math.max(bucket.samples, 1);
    return {
      t,
      time: formatTime(t, longRange),
      used_memory: bucket.used_memory / samples,
      used_memory_rss: bucket.used_memory_rss / samples,
      used_memory_peak: bucket.used_memory_peak / samples,
      mem_fragmentation_ratio: bucket.mem_fragmentation_ratio / samples,
      total_keys: Math.round(bucket.total_keys / samples),
      ops_per_sec: bucket.ops_per_sec / samples,
      hit_rate_pct: bucket.hit_rate_pct / samples,
      connected_clients: Math.round(bucket.connected_clients / samples),
      blocked_clients: Math.round(bucket.blocked_clients / samples),
      input_kbps: bucket.input_kbps / samples,
      output_kbps: bucket.output_kbps / samples,
    };
  });
}

const chartCardStyle = { borderColor: '#2e3450' };
const chartHeight = 260;
const tooltipStyle = { background: '#252a40', border: '1px solid #2e3450', borderRadius: 6 };

function RuntimeMetricCard({ title, value, icon, color }: { title: string; value: string | number; icon: React.ReactNode; color?: string }) {
  return (
    <Card size="small" style={{ borderColor: '#2e3450' }}>
      <div style={{ minHeight: 78, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 6 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, color: color || '#8b90a8', fontSize: 13 }}>
          <span style={{ fontSize: 20, lineHeight: 1 }}>{icon}</span>
          <span>{title}</span>
        </div>
        <div title={String(value)} style={{ fontSize: 20, fontWeight: 700, lineHeight: 1.2, maxWidth: '100%', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
          {value}
        </div>
      </div>
    </Card>
  );
}

const Statistics: React.FC = () => {
  const [scope, setScope] = useState<Scope>('cluster');
  const [selectedNode, setSelectedNode] = useState<string>('');
  const [selectedRange, setSelectedRange] = useState(1);
  const [redisRange, setRedisRange] = useState<RedisRange>('1h');
  const [clusterInfo, setClusterInfo] = useState<ClusterMonitorInfo | null>(null);
  const [chartData, setChartData] = useState<ChartPoint[]>([]);
  const [redisChartData, setRedisChartData] = useState<RedisChartPoint[]>([]);
  const [initialLoading, setInitialLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [redisRefreshing, setRedisRefreshing] = useState(false);
  const [autoRefresh, setAutoRefresh] = useState(true);
  const [visibleSeries, setVisibleSeries] = useState<SeriesKey[]>(['mpt', 'usr', 'pull', 'push']);

  const range = TIME_RANGES[selectedRange];
  const redisRangeInfo = REDIS_RANGES.find((item) => item.value === redisRange) || REDIS_RANGES[0];

  const loadRuntimeData = useCallback(async (showLoading = false) => {
    if (showLoading) setInitialLoading(true);
    else setRefreshing(true);
    try {
      const cluster = await getClusterMonitor();
      setClusterInfo(cluster);

      const nodes = cluster.nodes || [];
      const targetNode = scope === 'node' ? (selectedNode || nodes[0]?.uid || '') : '';
      if (scope === 'node' && !selectedNode && targetNode) {
        setSelectedNode(targetNode);
      }

      const nodeIds = scope === 'node'
        ? (targetNode ? [targetNode] : [])
        : nodes.map((node) => node.uid);

      const histories = await Promise.all(nodeIds.map(async (nodeId) => {
        const history = await getNodeHistory(nodeId, range.limit, range.range).catch(() => [] as NodeHistorySnapshot[]);
        return { history };
      }));
      const longRange = range.range !== 'raw' || range.duration > 86400;
      setChartData(aggregateHistory(histories, range.duration, range.bucket, longRange));
    } catch (error) {
      console.error('Failed to load runtime statistics', error);
      setChartData(aggregateHistory([], range.duration, range.bucket, range.duration > 86400));
    } finally {
      setInitialLoading(false);
      setRefreshing(false);
    }
  }, [scope, selectedNode, range.limit, range.range, range.duration, range.bucket]);

  const loadRedisData = useCallback(async (showLoading = false) => {
    setRedisRefreshing(true);
    try {
      const result = await getRedisHistory(redisRange);
      const points = (result.items || []).filter((item): item is RedisStatPoint => typeof item === 'object' && item !== null);
      setRedisChartData(aggregateRedisHistory(points, redisRangeInfo.duration, redisRangeInfo.bucket, redisRangeInfo.value === '7d'));
    } catch (error) {
      console.error('Failed to load redis history', error);
      setRedisChartData(aggregateRedisHistory([], redisRangeInfo.duration, redisRangeInfo.bucket, redisRangeInfo.value === '7d'));
    } finally {
      if (showLoading) setInitialLoading(false);
      setRedisRefreshing(false);
    }
  }, [redisRange, redisRangeInfo.duration, redisRangeInfo.bucket, redisRangeInfo.value]);

  useEffect(() => {
    loadRuntimeData(true);
    if (!autoRefresh) return undefined;
    const timer = window.setInterval(() => loadRuntimeData(false), range.range === 'raw' ? 5000 : 30000);
    return () => window.clearInterval(timer);
  }, [loadRuntimeData, range.range, autoRefresh]);

  useEffect(() => {
    loadRedisData(false);
    if (!autoRefresh) return undefined;
    const timer = window.setInterval(() => loadRedisData(false), 60000);
    return () => window.clearInterval(timer);
  }, [loadRedisData, autoRefresh]);

  const nodeOptions = useMemo(() => (
    (clusterInfo?.nodes || []).map((node) => ({ label: node.node_name || node.uid, value: node.uid }))
  ), [clusterInfo]);

  const currentNode = useMemo(() => (
    (clusterInfo?.nodes || []).find((node) => node.uid === selectedNode)
  ), [clusterInfo, selectedNode]);

  const current = useMemo(() => {
    if (!clusterInfo) return null;
    if (scope === 'node' && currentNode) {
      if (!currentNode.online) {
        return { cpu: 0, mem: 0, mpt: 0, usr: 0, pull: 0, push: 0, recv: 0, send: 0 };
      }
      return {
        cpu: normalizeCpuPercent(currentNode.cpu || 0),
        mem: currentNode.mem || 0,
        mpt: currentNode.mpt || 0,
        usr: currentNode.usr || 0,
        pull: currentNode.pull || 0,
        push: currentNode.push || 0,
        recv: currentNode.recv_speed || 0,
        send: currentNode.send_speed || 0,
      };
    }
    const onlineNodes = Math.max(clusterInfo.online_nodes || 0, 1);
    const totalCpu = (clusterInfo.nodes || [])
      .filter((node) => node.online)
      .reduce((sum, node) => sum + normalizeCpuPercent(node.cpu || 0), 0);
    return {
      cpu: normalizeCpuPercent(totalCpu / onlineNodes),
      mem: clusterInfo.total_mem || 0,
      mpt: clusterInfo.total_servers || 0,
      usr: clusterInfo.total_clients || 0,
      pull: clusterInfo.total_pull || 0,
      push: clusterInfo.total_push || 0,
      recv: clusterInfo.total_recv_speed || 0,
      send: clusterInfo.total_send_speed || 0,
    };
  }, [clusterInfo, currentNode, scope]);

  const cpuColor = (current?.cpu || 0) > 85 ? '#ff4d4f' : (current?.cpu || 0) > 55 ? '#faad14' : '#52c41a';

  const handleRefresh = () => {
    loadRuntimeData(false);
    loadRedisData(false);
  };

  const renderRuntimeChart = (dataKey: keyof ChartPoint, name: string, color: string, formatter: (value: number) => string) => (
    <ResponsiveContainer width="100%" height={chartHeight}>
      <LineChart data={chartData}>
        <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
        <XAxis dataKey="time" stroke="#6b7194" tick={{ fontSize: 11 }} minTickGap={28} />
        <YAxis stroke="#6b7194" tick={{ fontSize: 11 }} tickFormatter={(value) => formatter(Number(value))} />
        <Tooltip contentStyle={tooltipStyle} formatter={(value) => [formatter(Number(value)), name]} />
        <Line type="monotone" dataKey={dataKey as string} name={name} stroke={color} strokeWidth={2} dot={false} isAnimationActive={false} />
      </LineChart>
    </ResponsiveContainer>
  );

  const renderResourceChart = (lines: Array<{ key: SeriesKey; name: string; color: string }>) => (
    <ResponsiveContainer width="100%" height={chartHeight}>
      <LineChart data={chartData}>
        <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
        <XAxis dataKey="time" stroke="#6b7194" tick={{ fontSize: 11 }} minTickGap={28} />
        <YAxis stroke="#6b7194" tick={{ fontSize: 11 }} allowDecimals={false} />
        <Tooltip contentStyle={tooltipStyle} />
        <Legend />
        {lines.filter((line) => visibleSeries.includes(line.key)).map((line) => (
          <Line key={line.key} type="monotone" dataKey={line.key} name={line.name} stroke={line.color} strokeWidth={2} dot={false} isAnimationActive={false} />
        ))}
      </LineChart>
    </ResponsiveContainer>
  );

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12, alignItems: 'center', marginBottom: 16, flexWrap: 'wrap' }}>
        <Title level={4} style={{ margin: 0 }}>运行状态统计</Title>
        <div style={{ display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap' }}>
          <Segmented value={scope} onChange={(value) => setScope(value as Scope)} options={[{ label: '集群', value: 'cluster' }, { label: '节点', value: 'node' }]} />
          {scope === 'node' && (
            <Select value={selectedNode || undefined} options={nodeOptions} placeholder="选择节点" onChange={setSelectedNode} style={{ width: 220 }} />
          )}
          <Segmented value={selectedRange} onChange={(value) => setSelectedRange(value as number)} options={TIME_RANGES.map((item, index) => ({ label: item.label, value: index }))} />
          <Switch checked={autoRefresh} onChange={setAutoRefresh} checkedChildren="自动" unCheckedChildren="手动" />
          <Button icon={<ReloadOutlined />} loading={refreshing || redisRefreshing} onClick={handleRefresh}>刷新</Button>
        </div>
      </div>

      <Spin spinning={initialLoading && chartData.length === 0}>
        <Row gutter={[16, 16]}>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="CPU" value={formatCpuPercent(current?.cpu || 0)} icon={<DashboardOutlined />} color={cpuColor} /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="内存" value={formatBytes(current?.mem || 0)} icon={<HddOutlined />} /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="在线基站" value={current?.mpt || 0} icon={<CloudServerOutlined />} /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="在线用户" value={current?.usr || 0} icon={<UserOutlined />} /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="有效 Pull" value={current?.pull || 0} icon={<SwapOutlined />} /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="有效 Push" value={current?.push || 0} icon={<SwapOutlined />} /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="输入" value={formatMbps(current?.recv || 0)} icon={<ArrowDownOutlined />} color="#ff7875" /></Col>
          <Col xs={12} sm={6} xl={3}><RuntimeMetricCard title="输出" value={formatMbps(current?.send || 0)} icon={<ArrowUpOutlined />} color="#52c41a" /></Col>
        </Row>

        <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12, alignItems: 'center', marginTop: 18, marginBottom: 10, flexWrap: 'wrap' }}>
          <Title level={5} style={{ margin: 0 }}>在线资源</Title>
          <Checkbox.Group
            value={visibleSeries}
            onChange={(value) => setVisibleSeries(value as SeriesKey[])}
            options={[{ label: '基站', value: 'mpt' }, { label: '用户', value: 'usr' }, { label: 'Pull', value: 'pull' }, { label: 'Push', value: 'push' }]}
          />
        </div>
        <Row gutter={[16, 16]}>
          <Col xs={24} lg={12}>
            <Card title="基站 / 用户" style={chartCardStyle} size="small">
              {chartData.length > 0 ? renderResourceChart([
                { key: 'mpt', name: '基站', color: '#4a8eff' },
                { key: 'usr', name: '用户', color: '#52c41a' },
              ]) : <Empty description="暂无历史数据" />}
            </Card>
          </Col>
          <Col xs={24} lg={12}>
            <Card title="有效 Pull / Push" style={chartCardStyle} size="small">
              {chartData.length > 0 ? renderResourceChart([
                { key: 'pull', name: 'Pull', color: '#faad14' },
                { key: 'push', name: 'Push', color: '#eb2f96' },
              ]) : <Empty description="暂无历史数据" />}
            </Card>
          </Col>
        </Row>

        <Title level={5} style={{ margin: '18px 0 10px' }}>运行指标</Title>
        <Row gutter={[16, 16]}>
          <Col xs={24} lg={12}><Card title="CPU" style={chartCardStyle} size="small">{renderRuntimeChart('cpu', 'CPU', '#4a8eff', (value) => `${value.toFixed(1)}%`)}</Card></Col>
          <Col xs={24} lg={12}><Card title="内存" style={chartCardStyle} size="small">{renderRuntimeChart('mem', '内存', '#52c41a', formatBytes)}</Card></Col>
          <Col xs={24} lg={12}><Card title="输入带宽" style={chartCardStyle} size="small">{renderRuntimeChart('recv_speed', '输入', '#ff7875', formatSpeed)}</Card></Col>
          <Col xs={24} lg={12}><Card title="输出带宽" style={chartCardStyle} size="small">{renderRuntimeChart('send_speed', '输出', '#52c41a', formatSpeed)}</Card></Col>
        </Row>

        <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12, alignItems: 'center', marginTop: 18, marginBottom: 10, flexWrap: 'wrap' }}>
          <Title level={5} style={{ margin: 0 }}>Redis 历史状态</Title>
          <Segmented value={redisRange} onChange={(value) => setRedisRange(value as RedisRange)} options={REDIS_RANGES.map((item) => ({ label: item.label, value: item.value }))} />
        </div>
        {redisChartData.length > 0 ? (
          <Row gutter={[16, 16]}>
            <Col xs={24} lg={12}>
              <Card title="内存" style={chartCardStyle} size="small">
                <ResponsiveContainer width="100%" height={chartHeight}>
                  <LineChart data={redisChartData}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                    <XAxis dataKey="time" stroke="#6b7194" tick={{ fontSize: 11 }} minTickGap={28} />
                    <YAxis stroke="#6b7194" tick={{ fontSize: 11 }} tickFormatter={(value) => formatBytes(Number(value))} />
                    <Tooltip contentStyle={tooltipStyle} formatter={(value, name) => [formatBytes(Number(value)), name]} />
                    <Legend />
                    <Line type="monotone" dataKey="used_memory" name="已用" stroke="#4a8eff" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line type="monotone" dataKey="used_memory_rss" name="RSS" stroke="#52c41a" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line type="monotone" dataKey="used_memory_peak" name="峰值" stroke="#faad14" strokeWidth={2} dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              </Card>
            </Col>
            <Col xs={24} lg={12}>
              <Card title="连接 / Key" style={chartCardStyle} size="small">
                <ResponsiveContainer width="100%" height={chartHeight}>
                  <LineChart data={redisChartData}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                    <XAxis dataKey="time" stroke="#6b7194" tick={{ fontSize: 11 }} minTickGap={28} />
                    <YAxis stroke="#6b7194" tick={{ fontSize: 11 }} allowDecimals={false} />
                    <Tooltip contentStyle={tooltipStyle} />
                    <Legend />
                    <Line type="monotone" dataKey="connected_clients" name="客户端" stroke="#4a8eff" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line type="monotone" dataKey="blocked_clients" name="阻塞客户端" stroke="#eb2f96" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line type="monotone" dataKey="total_keys" name="Key 数" stroke="#52c41a" strokeWidth={2} dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              </Card>
            </Col>
            <Col xs={24} lg={12}>
              <Card title="请求" style={chartCardStyle} size="small">
                <ResponsiveContainer width="100%" height={chartHeight}>
                  <LineChart data={redisChartData}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                    <XAxis dataKey="time" stroke="#6b7194" tick={{ fontSize: 11 }} minTickGap={28} />
                    <YAxis stroke="#6b7194" tick={{ fontSize: 11 }} />
                    <Tooltip contentStyle={tooltipStyle} formatter={(value) => [`${Number(value).toFixed(1)} op/s`, 'QPS']} />
                    <Line type="monotone" dataKey="ops_per_sec" name="QPS" stroke="#faad14" strokeWidth={2} dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              </Card>
            </Col>
            <Col xs={24} lg={12}>
              <Card title="网络 / 命中" style={chartCardStyle} size="small">
                <ResponsiveContainer width="100%" height={chartHeight}>
                  <LineChart data={redisChartData}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                    <XAxis dataKey="time" stroke="#6b7194" tick={{ fontSize: 11 }} minTickGap={28} />
                    <YAxis yAxisId="kbps" stroke="#6b7194" tick={{ fontSize: 11 }} tickFormatter={(value) => `${Number(value).toFixed(1)} KB/s`} />
                    <YAxis yAxisId="rate" orientation="right" stroke="#52c41a" tick={{ fontSize: 11 }} tickFormatter={(value) => `${Number(value).toFixed(0)}%`} />
                    <Tooltip
                      contentStyle={tooltipStyle}
                      formatter={(value, name) => (name === '命中率' ? [`${Number(value).toFixed(1)}%`, name] : [`${Number(value).toFixed(1)} KB/s`, name])}
                    />
                    <Legend />
                    <Line yAxisId="kbps" type="monotone" dataKey="input_kbps" name="输入" stroke="#ff7875" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line yAxisId="kbps" type="monotone" dataKey="output_kbps" name="输出" stroke="#4a8eff" strokeWidth={2} dot={false} isAnimationActive={false} />
                    <Line yAxisId="rate" type="monotone" dataKey="hit_rate_pct" name="命中率" stroke="#52c41a" strokeWidth={2} dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              </Card>
            </Col>
          </Row>
        ) : (
          <div style={{ height: chartHeight, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            <Text type="secondary">暂无 Redis 历史数据</Text>
          </div>
        )}
      </Spin>
    </div>
  );
};

export default Statistics;