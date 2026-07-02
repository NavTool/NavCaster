import React, { useEffect, useState, useCallback } from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Segmented } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useMultiSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { CasterNode } from '../api/types';
import { formatBytes, formatCpuPercent, formatOnlineTime, formatDelay, formatSpeed, normalizeCpuPercent } from '../utils/format';
import { getNodeHistory, type NodeHistorySnapshot } from '../api';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend } from 'recharts';

const { Title } = Typography;

const TIME_RANGES = [
  { label: '1小时', duration: 3600, limit: 720, range: 'raw' as const, bucket: 5 },
  { label: '6小时', duration: 21600, limit: 4320, range: 'raw' as const, bucket: 30 },
  { label: '24小时', duration: 86400, limit: 17280, range: 'raw' as const, bucket: 120 },
  { label: '7天', duration: 604800, limit: 120960, range: 'raw' as const, bucket: 900 },
  { label: '30天', duration: 2592000, limit: 43200, range: '1m' as const, bucket: 3600 },
];

function formatTime(ts: number, longRange?: boolean): string {
  const d = new Date(ts * 1000);
  if (longRange) {
    return `${String(d.getMonth() + 1).padStart(2, '0')}/${String(d.getDate()).padStart(2, '0')} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  }
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

const chartCardStyle = { borderColor: '#2e3450' };
const chartHeight = 220;

interface ChartPoint {
  time: string;
  cpu: number;
  mem: number;
  mpt: number;
  usr: number;
  send: number;
  recv: number;
  delay: number;
}

function resolveTimelineEnd(history: NodeHistorySnapshot[], bucketSeconds: number) {
  const end = Math.floor(Date.now() / 1000);
  const latest = history.reduce((max, snapshot) => Math.max(max, snapshot.t || 0), 0);
  const staleAfter = Math.max(bucketSeconds * 3, 60);
  return Math.floor((latest > 0 && end - latest <= staleAfter ? latest : end) / bucketSeconds) * bucketSeconds;
}

function buildChartData(history: NodeHistorySnapshot[], durationSeconds: number, bucketSeconds: number, longRange: boolean) {
  const end = resolveTimelineEnd(history, bucketSeconds);
  const start = end - durationSeconds;
  const firstBucket = Math.ceil(start / bucketSeconds) * bucketSeconds;
  const lastBucket = Math.floor(end / bucketSeconds) * bucketSeconds;
  const shortGapSeconds = Math.max(bucketSeconds * 3, 60);
  const buckets = new Map<number, {
    samples: number; cpu: number; mem: number; mpt: number; usr: number; send: number; recv: number; delay: number;
  }>();

  history.forEach((snapshot) => {
    if (snapshot.t < start || snapshot.t > end) return;
    const bucket = Math.floor(snapshot.t / bucketSeconds) * bucketSeconds;
    const current = buckets.get(bucket) || { samples: 0, cpu: 0, mem: 0, mpt: 0, usr: 0, send: 0, recv: 0, delay: 0 };
    current.samples += 1;
    current.cpu += normalizeCpuPercent(snapshot.cpu || 0);
    current.mem += snapshot.mem || 0;
    current.mpt += snapshot.mpt || 0;
    current.usr += snapshot.usr || 0;
    current.send += snapshot.send_speed || 0;
    current.recv += snapshot.recv_speed || 0;
    current.delay += snapshot.q_delay || 0;
    buckets.set(bucket, current);
  });

  const points: ChartPoint[] = [];
  let lastSeen = 0;
  let lastPoint: Omit<ChartPoint, 'time'> | null = null;
  for (let t = firstBucket; t <= lastBucket; t += bucketSeconds) {
    const bucket = buckets.get(t);
    let point: Omit<ChartPoint, 'time'>;
    if (bucket) {
      const samples = Math.max(bucket.samples, 1);
      point = {
        cpu: +(bucket.cpu / samples).toFixed(1),
        mem: +(bucket.mem / samples / 1024 / 1024).toFixed(1),
        mpt: Math.round(bucket.mpt / samples),
        usr: Math.round(bucket.usr / samples),
        send: +(bucket.send / samples / 1024).toFixed(1),
        recv: +(bucket.recv / samples / 1024).toFixed(1),
        delay: bucket.delay / samples,
      };
      lastSeen = t;
      lastPoint = point;
    } else if (lastPoint && t - lastSeen <= shortGapSeconds) {
      point = lastPoint;
    } else {
      point = { cpu: 0, mem: 0, mpt: 0, usr: 0, send: 0, recv: 0, delay: 0 };
    }
    points.push({ time: formatTime(t, longRange), ...point });
  }
  return points;
}

const NodeDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: sseData } = useMultiSSE<{ nodes: Record<string, CasterNode> }>(['nodes']);
  const node = sseData.nodes?.[id || ''];

  const [chartData, setChartData] = useState<ChartPoint[]>([]);
  const [selectedRange, setSelectedRange] = useState(0); // index into TIME_RANGES

  const currentRange = TIME_RANGES[selectedRange];

  const loadHistory = useCallback(async () => {
    if (!id) return;
    try {
      const r = TIME_RANGES[selectedRange];
      const data = await getNodeHistory(id, r.limit, r.range);
      const longRange = r.range !== 'raw' || r.duration > 86400;
      setChartData(buildChartData(data, r.duration, r.bucket, longRange));
    } catch { /* ignore */ }
  }, [id, selectedRange]);

  useEffect(() => {
    loadHistory();
    // 短时间范围(raw)5秒刷新, 长时间范围降低刷新频率
    const interval = currentRange.range === 'raw' ? 5000 : 30000;
    const timer = setInterval(loadHistory, interval);
    return () => clearInterval(timer);
  }, [loadHistory, currentRange.range]);

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/dashboard">节点状态</Link> },
        { title: node?.node_name || id || '节点详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{node?.node_name || id}</Title>

      {node ? (
        <>
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="状态" valueRender={() => <StatusIndicator status="online" pulse label="在线" />} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="CPU" value={formatCpuPercent(node.cpu_usage || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="内存" value={formatBytes(node.mem_usage || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="运行时长" value={formatOnlineTime(node.online_time || 0)} />
              </Card>
            </Col>
          </Row>
          <Row gutter={[16, 16]}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="基站数" value={node.server_count} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="移动站数" value={node.client_count} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="输入" value={formatSpeed(node.recv_speed || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="输出" value={formatSpeed(node.send_speed || 0)} />
              </Card>
            </Col>
          </Row>
          <Card title="延迟信息" style={{ marginTop: 16, borderColor: '#2e3450' }}>
            <Row gutter={16}>
              <Col span={8}><Statistic title="处理延迟" value={formatDelay(node.queue_delay || 0)} /></Col>
              <Col span={8}><Statistic title="PUB TCP" value={formatDelay(node.pub_tcp_delay || 0)} /></Col>
              <Col span={8}><Statistic title="SUB TCP" value={formatDelay(node.sub_tcp_delay || 0)} /></Col>
            </Row>
          </Card>

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12, marginTop: 16, marginBottom: 12, flexWrap: 'wrap' }}>
            <Title level={5} style={{ margin: 0 }}>历史趋势</Title>
            <Segmented options={TIME_RANGES.map((r, i) => ({ label: r.label, value: i }))}
              value={selectedRange} onChange={v => setSelectedRange(v as number)} size="small" />
          </div>
            {chartData.length > 0 ? (
              <>
                <Row gutter={[16, 16]}>
                  <Col xs={24} lg={12}>
                    <Card size="small" title="CPU (%)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Line type="monotone" dataKey="cpu" stroke="#4a8eff" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col xs={24} lg={12}>
                    <Card size="small" title="内存 (MB)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Line type="monotone" dataKey="mem" stroke="#52c41a" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col xs={24} lg={12}>
                    <Card size="small" title="连接数" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Legend />
                          <Line type="monotone" dataKey="mpt" name="基站" stroke="#4a8eff" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                          <Line type="monotone" dataKey="usr" name="用户" stroke="#faad14" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col xs={24} lg={12}>
                    <Card size="small" title="流量 (KB/s)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Legend />
                          <Line type="monotone" dataKey="recv" name="接收" stroke="#52c41a" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                          <Line type="monotone" dataKey="send" name="发送" stroke="#ff4d4f" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                </Row>
              </>
            ) : (
              <Empty description="暂无历史数据，数据每分钟采集一次" />
            )}
        </>
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到节点 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default NodeDetail;
