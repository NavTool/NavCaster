import React, { useEffect, useState, useCallback } from 'react';
import { Card, Col, Row, Typography, Spin, Table, Tag, Progress, Descriptions, Tooltip } from 'antd';
import {
  DatabaseOutlined, ClusterOutlined, CloudServerOutlined,
  DashboardOutlined, HddOutlined, ThunderboltOutlined,
  CheckCircleOutlined, FieldTimeOutlined, ApiOutlined,
  CrownOutlined,
} from '@ant-design/icons';
import {
  getRedisMonitor, getRedisKeys, getClusterMonitor,
  type RedisMonitorInfo, type RedisKeysAnalysis, type ClusterMonitorInfo,
} from '../api';
import { formatDuration } from '../utils/format';
import MetricCard from '../components/MetricCard';

const { Title, Text } = Typography;

function formatTimestamp(ts: number): string {
  if (!ts) return '-';
  return new Date(ts * 1000).toLocaleString('zh-CN');
}

function formatMemory(bytes: number): string {
  if (!bytes) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return (bytes / Math.pow(k, i)).toFixed(1) + ' ' + sizes[i];
}

function formatOps(ops: number): string {
  if (ops >= 1000000) return (ops / 1000000).toFixed(1) + 'M';
  if (ops >= 1000) return (ops / 1000).toFixed(1) + 'K';
  return String(ops);
}

const SystemMonitor: React.FC = () => {
  const [redisInfo, setRedisInfo] = useState<RedisMonitorInfo | null>(null);
  const [keysInfo, setKeysInfo] = useState<RedisKeysAnalysis | null>(null);
  const [clusterInfo, setClusterInfo] = useState<ClusterMonitorInfo | null>(null);
  const [loading, setLoading] = useState(true);

  const fetchAll = useCallback(async () => {
    try {
      const [redis, keys, cluster] = await Promise.all([
        getRedisMonitor(),
        getRedisKeys(),
        getClusterMonitor(),
      ]);
      setRedisInfo(redis);
      setKeysInfo(keys);
      setClusterInfo(cluster);
    } catch {
      /* silent */
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchAll();
    const iv = setInterval(fetchAll, 10000);
    return () => clearInterval(iv);
  }, [fetchAll]);

  if (loading) return <Spin size="large" style={{ display: 'block', margin: '100px auto' }} />;

  return (
    <div>
      <Title level={4} style={{ margin: '0 0 20px' }}>系统监控</Title>

      {/* =============== Cluster Overview =============== */}
      {clusterInfo && (
        <>
          <Title level={5} style={{ margin: '0 0 12px' }}>
            <ClusterOutlined style={{ marginRight: 8 }} />集群总览
          </Title>
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="节点" value={clusterInfo.online_nodes} prefix={<CloudServerOutlined />} suffix={`/ ${clusterInfo.total_nodes}`} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="基站" value={clusterInfo.total_servers} prefix={<DatabaseOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="用户" value={clusterInfo.total_clients} prefix={<ApiOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="Pull" value={clusterInfo.total_pull} prefix={<ThunderboltOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="Push" value={clusterInfo.total_push} prefix={<ThunderboltOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="Redis 延迟" value={clusterInfo.redis_latency_ms.toFixed(2)} suffix="ms" prefix={<FieldTimeOutlined />} />
            </Col>
          </Row>

          {/* Node cards */}
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            {clusterInfo.nodes.map((node) => (
              <Col xs={24} sm={12} md={8} key={node.uid}>
                <Card
                  size="small"
                  title={
                    <span>
                      {node.is_master && <CrownOutlined style={{ color: '#faad14', marginRight: 6 }} />}
                      {node.node_name || node.uid}
                      {node.is_master && <Tag color="gold" style={{ marginLeft: 8, fontSize: 11 }}>Master</Tag>}
                    </span>
                  }
                  style={{ borderColor: node.is_master ? '#faad14' : '#2e3450' }}
                >
                  <Row gutter={8}>
                    <Col span={12}>
                      <Text type="secondary" style={{ fontSize: 12 }}>CPU</Text>
                      <Progress percent={Number(node.cpu.toFixed(1))} size="small" strokeColor={node.cpu > 80 ? '#ff4d4f' : node.cpu > 50 ? '#faad14' : '#52c41a'} />
                    </Col>
                    <Col span={12}>
                      <Text type="secondary" style={{ fontSize: 12 }}>内存</Text>
                      <div>{formatMemory(node.mem)}</div>
                    </Col>
                  </Row>
                  <Row gutter={8} style={{ marginTop: 8 }}>
                    <Col span={6}><Text type="secondary" style={{ fontSize: 12 }}>基站</Text><div>{node.mpt}</div></Col>
                    <Col span={6}><Text type="secondary" style={{ fontSize: 12 }}>用户</Text><div>{node.usr}</div></Col>
                    <Col span={6}><Text type="secondary" style={{ fontSize: 12 }}>Pull</Text><div>{node.pull}</div></Col>
                    <Col span={6}><Text type="secondary" style={{ fontSize: 12 }}>Push</Text><div>{node.push}</div></Col>
                  </Row>
                  <div style={{ marginTop: 8, fontSize: 12, color: '#666' }}>
                    端口 {node.listen_port || '-'} · PID {node.process_id || '-'} · 运行 {formatDuration(node.uptime_seconds)}
                  </div>
                  <div style={{ marginTop: 6, fontSize: 12, color: '#666' }}>
                    {node.tag_version} · 队列延迟 {node.queue_delay}μs · 最近更新 {formatTimestamp(node.update_time)}
                  </div>
                </Card>
              </Col>
            ))}
          </Row>

          <Card title="节点详情" size="small" style={{ marginBottom: 24, borderColor: '#2e3450' }}>
            <Table<ClusterMonitorInfo['nodes'][number]>
              dataSource={clusterInfo.nodes}
              rowKey="uid"
              size="small"
              pagination={false}
              scroll={{ x: 1400 }}
              columns={[
                {
                  title: '节点',
                  dataIndex: 'node_name',
                  key: 'node_name',
                  fixed: 'left',
                  width: 180,
                  render: (_, node) => (
                    <span>
                      {node.node_name}
                      {node.is_master && <Tag color="gold" style={{ marginLeft: 8 }}>Master</Tag>}
                    </span>
                  ),
                },
                { title: 'UID', dataIndex: 'uid', key: 'uid', width: 100 },
                { title: '监听端口', dataIndex: 'listen_port', key: 'listen_port', width: 100 },
                { title: 'HTTP 端口', dataIndex: 'http_port', key: 'http_port', width: 100 },
                { title: '进程 ID', dataIndex: 'process_id', key: 'process_id', width: 100 },
                { title: '运行时长', dataIndex: 'uptime_seconds', key: 'uptime_seconds', width: 120, render: (v: number) => formatDuration(v) },
                { title: '最近更新', dataIndex: 'update_time', key: 'update_time', width: 170, render: (v: number) => formatTimestamp(v) },
                { title: '平台', dataIndex: 'run_platform', key: 'run_platform', width: 140 },
                { title: '版本', dataIndex: 'tag_version', key: 'tag_version', width: 160 },
                { title: '连接', dataIndex: 'conn', key: 'conn', width: 80 },
                { title: '基站', dataIndex: 'mpt', key: 'mpt', width: 80 },
                { title: '用户', dataIndex: 'usr', key: 'usr', width: 80 },
                { title: 'Pull', dataIndex: 'pull', key: 'pull', width: 80 },
                { title: 'Push', dataIndex: 'push', key: 'push', width: 80 },
                { title: 'CPU', dataIndex: 'cpu', key: 'cpu', width: 90, render: (v: number) => `${v.toFixed(1)}%` },
                { title: '内存', dataIndex: 'mem', key: 'mem', width: 110, render: (v: number) => formatMemory(v) },
                { title: '接收', dataIndex: 'recv_speed', key: 'recv_speed', width: 110, render: (v: number) => `${formatMemory(v)}/s` },
                { title: '发送', dataIndex: 'send_speed', key: 'send_speed', width: 110, render: (v: number) => `${formatMemory(v)}/s` },
              ]}
            />
          </Card>
        </>
      )}

      {/* =============== Redis Status =============== */}
      {redisInfo && (
        <>
          <Title level={5} style={{ margin: '0 0 12px' }}>
            <DatabaseOutlined style={{ marginRight: 8 }} />Redis 状态
          </Title>
          <Row gutter={[16, 16]} style={{ marginBottom: 16 }}>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="版本" value={redisInfo.server.redis_version} prefix={<HddOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="内存" value={redisInfo.memory.used_memory_human} prefix={<DashboardOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="连接数" value={redisInfo.clients.connected_clients} prefix={<ApiOutlined />} />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="QPS" value={redisInfo.stats.instantaneous_ops_per_sec} prefix={<ThunderboltOutlined />} suffix="op/s" />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="命中率" value={(redisInfo.stats.hit_rate * 100).toFixed(1)} prefix={<CheckCircleOutlined />} suffix="%" />
            </Col>
            <Col xs={12} sm={8} md={4}>
              <MetricCard title="Key 数" value={redisInfo.total_keys} prefix={<DatabaseOutlined />} />
            </Col>
          </Row>

          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col xs={24} md={12}>
              <Card title="服务器信息" size="small" style={{ borderColor: '#2e3450' }}>
                <Descriptions column={1} size="small" labelStyle={{ color: '#8b90a8' }}>
                  <Descriptions.Item label="运行时间">{formatDuration(redisInfo.server.uptime_in_seconds)}</Descriptions.Item>
                  <Descriptions.Item label="TCP 端口">{redisInfo.server.tcp_port}</Descriptions.Item>
                  <Descriptions.Item label="操作系统">{redisInfo.server.os}</Descriptions.Item>
                  <Descriptions.Item label="进程 ID">{redisInfo.server.process_id}</Descriptions.Item>
                  <Descriptions.Item label="角色">
                    <Tag color={redisInfo.replication.role === 'master' ? 'green' : 'blue'}>
                      {redisInfo.replication.role}
                    </Tag>
                  </Descriptions.Item>
                  <Descriptions.Item label="从节点">{redisInfo.replication.connected_slaves}</Descriptions.Item>
                </Descriptions>
              </Card>
            </Col>
            <Col xs={24} md={12}>
              <Card title="内存详情" size="small" style={{ borderColor: '#2e3450' }}>
                <Descriptions column={1} size="small" labelStyle={{ color: '#8b90a8' }}>
                  <Descriptions.Item label="已用内存">{redisInfo.memory.used_memory_human}</Descriptions.Item>
                  <Descriptions.Item label="RSS 内存">{redisInfo.memory.used_memory_rss_human}</Descriptions.Item>
                  <Descriptions.Item label="内存峰值">{redisInfo.memory.used_memory_peak_human}</Descriptions.Item>
                  <Descriptions.Item label="碎片率">
                    <Tag color={redisInfo.memory.mem_fragmentation_ratio > 1.5 ? 'orange' : 'green'}>
                      {redisInfo.memory.mem_fragmentation_ratio.toFixed(2)}
                    </Tag>
                  </Descriptions.Item>
                  <Descriptions.Item label="总命令数">{formatOps(redisInfo.stats.total_commands_processed)}</Descriptions.Item>
                  <Descriptions.Item label="总连接数">{formatOps(redisInfo.stats.total_connections_received)}</Descriptions.Item>
                </Descriptions>
              </Card>
            </Col>
          </Row>
        </>
      )}

      {/* =============== Key Space Analysis =============== */}
      {keysInfo && (
        <>
          <Title level={5} style={{ margin: '0 0 12px' }}>
            <HddOutlined style={{ marginRight: 8 }} />Key 空间分析
          </Title>
          <Card style={{ marginBottom: 24, borderColor: '#2e3450' }}>
            <div style={{ marginBottom: 12 }}>
              <Text type="secondary">
                共 {keysInfo.total_keys} 个 Key，总内存 {formatMemory(keysInfo.total_memory)}
              </Text>
            </div>
            <Table<RedisKeysAnalysis['categories'][number]>
              dataSource={keysInfo.categories}
              rowKey="prefix"
              size="small"
              pagination={false}
              columns={[
                {
                  title: '前缀',
                  dataIndex: 'prefix',
                  key: 'prefix',
                  render: (text: string) => <Text strong style={{ fontFamily: 'monospace' }}>{text}</Text>,
                },
                {
                  title: '类型',
                  dataIndex: 'type',
                  key: 'type',
                  width: 80,
                  render: (t: string) => (
                    <Tag color={t === 'hash' ? 'blue' : t === 'list' ? 'green' : t === 'string' ? 'orange' : 'default'}>
                      {t}
                    </Tag>
                  ),
                },
                { title: 'Key 数', dataIndex: 'count', key: 'count', width: 80, sorter: (a: { count: number }, b: { count: number }) => a.count - b.count },
                { title: '字段/元素数', dataIndex: 'fields', key: 'fields', width: 110, sorter: (a: { fields: number }, b: { fields: number }) => a.fields - b.fields },
                {
                  title: '内存',
                  dataIndex: 'memory',
                  key: 'memory',
                  width: 100,
                  sorter: (a: { memory: number }, b: { memory: number }) => a.memory - b.memory,
                  defaultSortOrder: 'descend' as const,
                  render: (bytes: number) => formatMemory(bytes),
                },
                {
                  title: '占比',
                  dataIndex: 'memory',
                  key: 'ratio',
                  width: 120,
                  render: (bytes: number) => {
                    const pct = keysInfo.total_memory > 0 ? (bytes / keysInfo.total_memory) * 100 : 0;
                    return (
                      <Tooltip title={`${pct.toFixed(1)}%`}>
                        <Progress percent={Number(pct.toFixed(1))} size="small" showInfo={pct > 5} />
                      </Tooltip>
                    );
                  },
                },
                {
                  title: '说明',
                  dataIndex: 'description',
                  key: 'description',
                  render: (text: string) => <Text type="secondary">{text || '-'}</Text>,
                },
              ]}
            />
          </Card>
        </>
      )}
    </div>
  );
};

export default SystemMonitor;
