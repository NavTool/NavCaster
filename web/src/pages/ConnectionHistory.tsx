import React, { useState, useEffect, useMemo } from 'react';
import { Table, Typography, Tabs, Tag, Input } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { HistoryOutlined } from '@ant-design/icons';
import { usePolling } from '../hooks/usePolling';
import { useNavigate } from 'react-router-dom';
import api from '../api/client';
import { formatOnlineTime } from '../utils/format';

const { Title, Text } = Typography;

interface HistoryRecord {
  name: string;
  mount?: string;
  connect_key: string;
  node_id: string;
  type: number;
  account: string;
  host: string;
  port: number;
  connect_time: number;
  last_update: number;
  disconnect_time: number;
}

const typeLabels: Record<number, { text: string; color: string }> = {
  1: { text: 'SERVER', color: 'blue' },
  2: { text: 'CLIENT', color: 'green' },
  3: { text: 'NEAREST', color: 'purple' },
  4: { text: 'ALIAS', color: 'orange' },
  5: { text: 'PULL', color: 'cyan' },
  6: { text: 'PUSH', color: 'magenta' },
};

function formatTimestamp(ts: number): string {
  if (!ts) return '-';
  return new Date(ts * 1000).toLocaleString('zh-CN');
}

function formatDuration(connectTime: number, endTime: number): string {
  if (!connectTime) return '-';
  const end = endTime || Math.floor(Date.now() / 1000);
  let seconds = end - connectTime;
  if (seconds < 0) seconds = 0;
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (d > 0) return `${d}d ${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

async function fetchServerLogs(): Promise<Record<string, HistoryRecord>> {
  const { data } = await api.get('/api/logs/servers');
  return data;
}

async function fetchClientLogs(): Promise<Record<string, HistoryRecord>> {
  const { data } = await api.get('/api/logs/clients');
  return data;
}

const ConnectionHistory: React.FC = () => {
  const navigate = useNavigate();
  const { data: serverData, loading: serverLoading } = usePolling(fetchServerLogs, 5000);
  const { data: clientData, loading: clientLoading } = usePolling(fetchClientLogs, 5000);
  const [search, setSearch] = useState('');

  const makeColumns = (isServer: boolean): ColumnsType<HistoryRecord & { key: string }> => [
    {
      title: isServer ? '挂载点' : '用户名', dataIndex: 'name', key: 'name', width: 120,
      sorter: (a, b) => a.name.localeCompare(b.name),
      render: (v) => <span style={{ fontFamily: 'monospace', fontWeight: 500 }}>{v}</span>,
    },
    ...(isServer ? [] : [{
      title: '挂载点', dataIndex: 'mount' as keyof HistoryRecord, key: 'mount', width: 120,
      render: (v: string) => <span style={{ fontFamily: 'monospace' }}>{v || '-'}</span>,
    } as ColumnsType<HistoryRecord & { key: string }>[number]]),
    {
      title: '类型', dataIndex: 'type', key: 'type', width: 80,
      render: (v: number) => {
        const t = typeLabels[v] || { text: `${v}`, color: 'default' };
        return <Tag color={t.color}>{t.text}</Tag>;
      },
    },
    {
      title: '账户', dataIndex: 'account', key: 'account', width: 100,
      render: (v) => v || '-',
    },
    {
      title: 'IP', dataIndex: 'host', key: 'host', width: 130,
      render: (v) => <span style={{ fontFamily: 'monospace', color: '#8b90a8' }}>{v || '-'}</span>,
    },
    {
      title: '节点', dataIndex: 'node_id', key: 'node_id', width: 80,
      render: (v) => <span style={{ color: '#8b90a8' }}>{v || '-'}</span>,
    },
    {
      title: '上线时间', dataIndex: 'connect_time', key: 'connect_time', width: 160,
      sorter: (a, b) => (b.connect_time || 0) - (a.connect_time || 0),
      defaultSortOrder: 'ascend',
      render: (v: number) => formatTimestamp(v),
    },
    {
      title: '下线时间', dataIndex: 'disconnect_time', key: 'disconnect_time', width: 160,
      render: (v: number) => v ? formatTimestamp(v) : <Tag color="green">在线</Tag>,
    },
    {
      title: '时长', key: 'duration', width: 120,
      render: (_, r) => formatDuration(r.connect_time, r.disconnect_time),
    },
    {
      title: '最后更新', dataIndex: 'last_update', key: 'last_update', width: 160,
      render: (v: number) => formatTimestamp(v),
    },
  ];

  const toDataSource = (data: Record<string, HistoryRecord> | null) => {
    if (!data) return [];
    return Object.entries(data)
      .map(([key, val]) => ({ ...val, key }))
      .filter(r => !search || r.name.toLowerCase().includes(search.toLowerCase()) || r.account?.toLowerCase().includes(search.toLowerCase()) || r.host?.includes(search))
      .sort((a, b) => (b.connect_time || 0) - (a.connect_time || 0));
  };

  const serverDS = useMemo(() => toDataSource(serverData), [serverData, search]);
  const clientDS = useMemo(() => toDataSource(clientData), [clientData, search]);
  const onlineServers = serverDS.filter(r => !r.disconnect_time).length;
  const onlineClients = clientDS.filter(r => !r.disconnect_time).length;

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <HistoryOutlined style={{ fontSize: 22, color: '#4a8eff' }} />
          <Title level={4} style={{ margin: 0 }}>连接历史</Title>
        </div>
        <Input.Search
          placeholder="搜索名称/账户/IP"
          allowClear
          style={{ width: 260 }}
          onSearch={setSearch}
          onChange={e => !e.target.value && setSearch('')}
        />
      </div>
      <Tabs
        items={[
          {
            key: 'servers',
            label: `基站 (${serverDS.length} 条 / ${onlineServers} 在线)`,
            children: (
              <div style={{ borderRadius: 8, border: '1px solid #2e3450', overflow: 'hidden' }}>
                <Table
                  columns={makeColumns(true)}
                  dataSource={serverDS}
                  loading={serverLoading}
                  size="small"
                  pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
                  scroll={{ x: 1100 }}
                  onRow={(record) => ({
                    onClick: () => navigate(`/history/server/${encodeURIComponent(record.name)}`),
                    style: { cursor: 'pointer' },
                  })}
                />
              </div>
            ),
          },
          {
            key: 'clients',
            label: `用户 (${clientDS.length} 条 / ${onlineClients} 在线)`,
            children: (
              <div style={{ borderRadius: 8, border: '1px solid #2e3450', overflow: 'hidden' }}>
                <Table
                  columns={makeColumns(false)}
                  dataSource={clientDS}
                  loading={clientLoading}
                  size="small"
                  pagination={{ pageSize: 50, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
                  scroll={{ x: 1200 }}
                  onRow={(record) => ({
                    onClick: () => navigate(`/history/client/${encodeURIComponent(record.name)}`),
                    style: { cursor: 'pointer' },
                  })}
                />
              </div>
            ),
          },
        ]}
      />
    </div>
  );
};

export default ConnectionHistory;
