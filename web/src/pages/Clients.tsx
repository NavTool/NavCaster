import React, { useState, useEffect, useMemo } from 'react';
import { Table, Tag, Typography } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { UserOutlined } from '@ant-design/icons';
import { useSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { ClientState, StreamState } from '../api/types';
import { formatOnlineTime, formatQuality, formatBytes, formatSpeed } from '../utils/format';
import { useNavigate } from 'react-router-dom';

const { Title, Text } = Typography;

const qualityEntries = [
  { value: 0, text: '无' },
  { value: 1, text: '单点' },
  { value: 2, text: 'DGPS' },
  { value: 4, text: '固定解' },
  { value: 5, text: '浮动解' },
];

const Clients: React.FC = () => {
  const navigate = useNavigate();
  const { data, connected } = useSSE<Record<string, ClientState>>('clients');
  const { data: streams } = useSSE<Record<string, StreamState>>('streams');
  const loading = !connected && !data;
  const [, setTick] = useState(0);

  // 1秒刷新在线时长
  useEffect(() => {
    const timer = setInterval(() => setTick(t => t + 1), 1000);
    return () => clearInterval(timer);
  }, []);

  const streamsMap = useMemo(() => streams || {}, [streams]);

  const columns: ColumnsType<ClientState & { key: string }> = [
    { title: '账号', dataIndex: 'account', key: 'account', width: 120,
      sorter: (a, b) => (a.account || '').localeCompare(b.account || ''),
      render: (v) => <span style={{ fontFamily: 'monospace', fontWeight: 500 }}>{v}</span>,
    },
    { title: '接入挂载点', dataIndex: 'login_mpt', key: 'login_mpt', width: 120,
      render: (v) => <span style={{ fontFamily: 'monospace' }}>{v}</span>,
    },
    { title: '使用挂载点', dataIndex: 'alias_mpt', key: 'alias_mpt', width: 120,
      render: (v) => <span style={{ fontFamily: 'monospace' }}>{v}</span>,
    },
    { title: 'IP', dataIndex: 'ip', key: 'ip', width: 120,
      render: (v) => <span style={{ fontFamily: 'monospace', color: '#8b90a8' }}>{v}</span>,
    },
    { title: '在线时长', key: 'online_time', width: 120,
      render: (_, r) => <span style={{ fontWeight: 500 }}>{formatOnlineTime(r.online_time)}</span>,
    },
    {
      title: '定位状态', key: 'quality', width: 90,
      render: (_, r) => {
        const q = formatQuality(r.quality);
        return <Tag style={{ borderRadius: 4, fontSize: 12 }} color={q.color}>{q.text}</Tag>;
      },
      filters: qualityEntries.map(e => ({ text: e.text, value: e.value })),
      onFilter: (value, record) => record.quality === value,
    },
    { title: '差分延迟', dataIndex: 'diff', key: 'diff', width: 90, render: (v) => v ? `${v.toFixed(1)} s` : '-' },
    { title: '发送带宽', key: 'send_speed', width: 120,
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return st ? formatSpeed(st.send_speed) : '-';
      },
    },
    { title: '发送流量', key: 'send_total', width: 120,
      render: (_, r) => {
        const st = streamsMap[r.uid];
        return st ? formatBytes(st.send_total) : '-';
      },
    },
  ];

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <UserOutlined style={{ fontSize: 22, color: '#4a8eff' }} />
          <Title level={4} style={{ margin: 0 }}>移动站</Title>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <StatusIndicator status="online" pulse size="sm" />
          <Text style={{ color: '#8b90a8', fontSize: 13 }}>{dataSource.length} 在线</Text>
        </div>
      </div>
      <div style={{ borderRadius: 8, border: '1px solid #2e3450', overflow: 'hidden' }}>
        <Table
          columns={columns}
          dataSource={dataSource}
          loading={loading}
          size="small"
          pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
          scroll={{ x: 1000 }}
          onRow={(record) => ({ onClick: () => navigate(`/clients/${encodeURIComponent(record.key)}`), style: { cursor: 'pointer' } })}
        />
      </div>
    </div>
  );
};

export default Clients;
