import React, { useEffect, useState, useCallback } from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty, Segmented } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useMultiSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { CasterNode } from '../api/types';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed } from '../utils/format';
import { getNodeHistory, type NodeHistorySnapshot } from '../api';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend } from 'recharts';

const { Title } = Typography;

const TIME_RANGES = [
  { label: '1小时', value: 720 },
  { label: '6小时', value: 4320 },
  { label: '12小时', value: 8640 },
  { label: '24小时', value: 17280 },
];

function formatTime(ts: number): string {
  const d = new Date(ts * 1000);
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

const chartCardStyle = { borderColor: '#2e3450', marginTop: 16 };
const chartHeight = 220;

const NodeDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: sseData } = useMultiSSE<{ nodes: Record<string, CasterNode> }>(['nodes']);
  const node = sseData.nodes?.[id || ''];

  const [history, setHistory] = useState<NodeHistorySnapshot[]>([]);
  const [range, setRange] = useState(60);

  const loadHistory = useCallback(async () => {
    if (!id) return;
    try {
      const data = await getNodeHistory(id, range);
      // API returns newest-first (LPUSH), reverse for chronological order
      setHistory([...data].reverse());
    } catch { /* ignore */ }
  }, [id, range]);

  useEffect(() => {
    loadHistory();
    const timer = setInterval(loadHistory, 5000);
    return () => clearInterval(timer);
  }, [loadHistory]);

  const chartData = history.map(s => ({
    time: formatTime(s.t),
    cpu: +s.cpu.toFixed(1),
    mem: +(s.mem / 1024 / 1024).toFixed(1),
    mpt: s.mpt,
    usr: s.usr,
    send: +(s.send_speed / 1024).toFixed(1),
    recv: +(s.recv_speed / 1024).toFixed(1),
    delay: s.q_delay,
  }));

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
                <Statistic title="CPU" value={`${(node.cpu_usage || 0).toFixed(1)}%`} />
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

          <Card title="历史趋势" style={chartCardStyle}
            extra={<Segmented options={TIME_RANGES.map(r => ({ label: r.label, value: r.value }))}
              value={range} onChange={v => setRange(v as number)} size="small" />}>
            {chartData.length > 0 ? (
              <>
                <Row gutter={16}>
                  <Col span={12}>
                    <Card size="small" title="CPU (%)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Line type="monotone" dataKey="cpu" stroke="#4a8eff" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col span={12}>
                    <Card size="small" title="内存 (MB)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Line type="monotone" dataKey="mem" stroke="#52c41a" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                </Row>
                <Row gutter={16}>
                  <Col span={12}>
                    <Card size="small" title="连接数" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Legend />
                          <Line type="monotone" dataKey="mpt" name="基站" stroke="#4a8eff" dot={false} strokeWidth={1.5} />
                          <Line type="monotone" dataKey="usr" name="用户" stroke="#faad14" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                  <Col span={12}>
                    <Card size="small" title="流量 (KB/s)" style={chartCardStyle}>
                      <ResponsiveContainer width="100%" height={chartHeight}>
                        <LineChart data={chartData}>
                          <CartesianGrid strokeDasharray="3 3" stroke="#2e3450" />
                          <XAxis dataKey="time" stroke="#8b90a8" fontSize={11} />
                          <YAxis stroke="#8b90a8" fontSize={11} />
                          <Tooltip contentStyle={{ background: '#1a1e34', border: '1px solid #2e3450' }} />
                          <Legend />
                          <Line type="monotone" dataKey="recv" name="接收" stroke="#52c41a" dot={false} strokeWidth={1.5} />
                          <Line type="monotone" dataKey="send" name="发送" stroke="#ff4d4f" dot={false} strokeWidth={1.5} />
                        </LineChart>
                      </ResponsiveContainer>
                    </Card>
                  </Col>
                </Row>
              </>
            ) : (
              <Empty description="暂无历史数据，数据每分钟采集一次" />
            )}
          </Card>
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
