import React, { useEffect, useState, useCallback } from 'react';
import { Card, Col, Row, Statistic, Table, Tag, Typography, Spin, Button } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import {
  ArrowLeftOutlined, ClockCircleOutlined, LinkOutlined,
  CloudServerOutlined, UserOutlined, ArrowUpOutlined, ArrowDownOutlined,
} from '@ant-design/icons';
import { useParams, useNavigate } from 'react-router-dom';
import { getMptHistory, getUsrHistory, type ConnectionHistoryItem } from '../api';
import { formatBytes } from '../utils/format';

const { Title, Text } = Typography;

function formatTimestamp(ts: number): string {
  if (!ts) return '-';
  return new Date(ts * 1000).toLocaleString('zh-CN');
}

function formatDuration(seconds: number): string {
  if (seconds <= 0) return '-';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (d > 0) return `${d}d ${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

const typeLabels: Record<number, { text: string; color: string }> = {
  1: { text: 'SERVER', color: 'blue' },
  2: { text: 'CLIENT', color: 'green' },
  3: { text: 'NEAREST', color: 'purple' },
  4: { text: 'ALIAS', color: 'orange' },
  5: { text: 'PULL', color: 'cyan' },
  6: { text: 'PUSH', color: 'magenta' },
};

const ConnectionHistoryDetail: React.FC = () => {
  const { type, name } = useParams<{ type: 'server' | 'client'; name: string }>();
  const navigate = useNavigate();
  const isServer = type === 'server';
  const decodedName = decodeURIComponent(name ?? '');

  const [records, setRecords] = useState<ConnectionHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchData = useCallback(async () => {
    if (!decodedName) return;
    try {
      const data = isServer
        ? await getMptHistory(decodedName)
        : await getUsrHistory(decodedName);
      setRecords(data);
    } catch {
      /* silent */
    } finally {
      setLoading(false);
    }
  }, [decodedName, isServer]);

  useEffect(() => { fetchData(); }, [fetchData]);

  // Aggregate statistics
  const now = Math.floor(Date.now() / 1000);
  const totalConnections = records.length;
  const onlineCount = records.filter(r => r.online).length;
  const totalDuration = records.reduce((sum, r) => {
    const end = r.disconnect_time || now;
    return sum + Math.max(0, end - r.connect_time);
  }, 0);
  const totalSend = records.reduce((sum, r) => sum + ((r as ConnectionHistoryItem & { send_total?: number }).send_total ?? 0), 0);
  const totalRecv = records.reduce((sum, r) => sum + ((r as ConnectionHistoryItem & { recv_total?: number }).recv_total ?? 0), 0);
  const lastSeen = records.length > 0 ? Math.max(...records.map(r => r.disconnect_time || r.last_update || 0)) : 0;

  const columns: ColumnsType<ConnectionHistoryItem & { key: string }> = [
    {
      title: '上线时间',
      dataIndex: 'connect_time',
      key: 'connect_time',
      width: 160,
      defaultSortOrder: 'descend',
      sorter: (a, b) => (a.connect_time || 0) - (b.connect_time || 0),
      render: (v: number) => formatTimestamp(v),
    },
    {
      title: '下线时间',
      dataIndex: 'disconnect_time',
      key: 'disconnect_time',
      width: 160,
      render: (v: number) => v ? formatTimestamp(v) : <Tag color="green">在线</Tag>,
    },
    {
      title: '时长',
      key: 'duration',
      width: 120,
      render: (_, r) => {
        const end = r.disconnect_time || now;
        return formatDuration(end - r.connect_time);
      },
    },
    ...(isServer ? [] : [{
      title: '挂载点',
      dataIndex: 'mount' as keyof ConnectionHistoryItem,
      key: 'mount',
      width: 120,
      render: (v: string) => <span style={{ fontFamily: 'monospace' }}>{v || '-'}</span>,
    } as ColumnsType<ConnectionHistoryItem & { key: string }>[number]]),
    {
      title: '类型',
      dataIndex: 'type',
      key: 'type',
      width: 80,
      render: (v: number) => {
        const t = typeLabels[v] || { text: `${v}`, color: 'default' };
        return <Tag color={t.color}>{t.text}</Tag>;
      },
    },
    {
      title: 'IP',
      dataIndex: 'host',
      key: 'host',
      width: 130,
      render: (v: string) => <span style={{ fontFamily: 'monospace', color: '#8b90a8' }}>{v || '-'}</span>,
    },
    {
      title: '节点',
      dataIndex: 'node_id',
      key: 'node_id',
      width: 80,
      render: (v: string) => <span style={{ color: '#8b90a8' }}>{v || '-'}</span>,
    },
    {
      title: '发送',
      dataIndex: 'send_total',
      key: 'send_total',
      width: 100,
      render: (v: number) => v ? formatBytes(v) : '-',
    },
    {
      title: '接收',
      dataIndex: 'recv_total',
      key: 'recv_total',
      width: 100,
      render: (v: number) => v ? formatBytes(v) : '-',
    },
  ];

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 20 }}>
        <Button
          type="text"
          icon={<ArrowLeftOutlined />}
          onClick={() => navigate('/history')}
          style={{ color: '#8b90a8' }}
        />
        {isServer
          ? <CloudServerOutlined style={{ fontSize: 22, color: '#4a8eff' }} />
          : <UserOutlined style={{ fontSize: 22, color: '#52c41a' }} />
        }
        <div>
          <Title level={4} style={{ margin: 0 }}>{decodedName}</Title>
          <Text style={{ color: '#6b7194', fontSize: 12 }}>
            {isServer ? '基准站' : '移动站'} · 连接历史详情
          </Text>
        </div>
      </div>

      <Spin spinning={loading}>
        {/* Summary cards */}
        <Row gutter={[16, 16]} style={{ marginBottom: 20 }}>
          <Col xs={12} sm={8} md={4}>
            <Card size="small" style={{ borderColor: '#2e3450', textAlign: 'center' }}>
              <Statistic
                title="总连接次数"
                value={totalConnections}
                prefix={<LinkOutlined />}
              />
            </Card>
          </Col>
          <Col xs={12} sm={8} md={4}>
            <Card size="small" style={{ borderColor: '#2e3450', textAlign: 'center' }}>
              <Statistic
                title="当前在线"
                value={onlineCount}
                valueStyle={{ color: onlineCount > 0 ? '#52c41a' : '#8b90a8' }}
              />
            </Card>
          </Col>
          <Col xs={12} sm={8} md={4}>
            <Card size="small" style={{ borderColor: '#2e3450', textAlign: 'center' }}>
              <Statistic
                title="累计在线时长"
                value={formatDuration(totalDuration)}
                prefix={<ClockCircleOutlined />}
              />
            </Card>
          </Col>
          <Col xs={12} sm={8} md={4}>
            <Card size="small" style={{ borderColor: '#2e3450', textAlign: 'center' }}>
              <Statistic
                title="总发送"
                value={formatBytes(totalSend)}
                prefix={<ArrowUpOutlined style={{ color: '#52c41a' }} />}
              />
            </Card>
          </Col>
          <Col xs={12} sm={8} md={4}>
            <Card size="small" style={{ borderColor: '#2e3450', textAlign: 'center' }}>
              <Statistic
                title="总接收"
                value={formatBytes(totalRecv)}
                prefix={<ArrowDownOutlined style={{ color: '#ff7875' }} />}
              />
            </Card>
          </Col>
          <Col xs={12} sm={8} md={4}>
            <Card size="small" style={{ borderColor: '#2e3450', textAlign: 'center' }}>
              <Statistic
                title="最后活跃"
                value={lastSeen > 0 ? new Date(lastSeen * 1000).toLocaleString('zh-CN', { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' }) : '-'}
              />
            </Card>
          </Col>
        </Row>

        {/* History table */}
        <Card
          title={`上下线记录（共 ${totalConnections} 条）`}
          size="small"
          style={{ borderColor: '#2e3450' }}
        >
          <Table
            columns={columns}
            dataSource={records.map((r, i) => ({ ...r, key: r.connect_key || String(i) }))}
            size="small"
            pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
            scroll={{ x: isServer ? 950 : 1100 }}
          />
        </Card>
      </Spin>
    </div>
  );
};

export default ConnectionHistoryDetail;
