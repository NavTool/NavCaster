import React, { useEffect, useState, useCallback } from 'react';
import { Typography, Card, Table, Input, Select, Tag, Space } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { getAuditLogs, type AuditLogEntry } from '../api';

const { Title } = Typography;

const ACTION_OPTIONS = [
  { label: '全部', value: '' },
  { label: '节点操作', value: 'node_action' },
];

const ACTION_COLORS: Record<string, string> = {
  node_action: 'blue',
};

function formatTime(ts: number): string {
  return new Date(ts * 1000).toLocaleString('zh-CN');
}

const AuditLog: React.FC = () => {
  const [items, setItems] = useState<AuditLogEntry[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(false);
  const [filterAction, setFilterAction] = useState('');
  const [filterUser, setFilterUser] = useState('');
  const [page, setPage] = useState(1);
  const pageSize = 50;

  const loadData = useCallback(async () => {
    setLoading(true);
    try {
      const params: Record<string, string | number> = { limit: pageSize, offset: (page - 1) * pageSize };
      if (filterAction) params.action = filterAction;
      if (filterUser) params.user = filterUser;
      const data = await getAuditLogs(params);
      setItems(data.items);
      setTotal(data.total);
    } catch { /* ignore */ }
    finally { setLoading(false); }
  }, [page, filterAction, filterUser]);

  useEffect(() => {
    loadData();
  }, [loadData]);

  const columns: ColumnsType<AuditLogEntry> = [
    {
      title: '时间',
      dataIndex: 'ts',
      width: 180,
      render: (ts: number) => formatTime(ts),
    },
    {
      title: '用户',
      dataIndex: 'user',
      width: 120,
    },
    {
      title: '操作',
      dataIndex: 'action',
      width: 120,
      render: (action: string) => (
        <Tag color={ACTION_COLORS[action] || 'default'}>{action}</Tag>
      ),
    },
    {
      title: '目标',
      dataIndex: 'target',
      width: 200,
    },
    {
      title: '详情',
      dataIndex: 'detail',
      render: (detail: Record<string, unknown>) => (
        <span style={{ fontSize: 12, color: '#8b90a8' }}>
          {JSON.stringify(detail)}
        </span>
      ),
    },
    {
      title: 'IP',
      dataIndex: 'ip',
      width: 140,
    },
    {
      title: '结果',
      dataIndex: 'result',
      width: 80,
      render: (result: string) => (
        <Tag color={result === 'ok' ? 'green' : 'red'}>{result}</Tag>
      ),
    },
  ];

  return (
    <div>
      <Title level={4}>操作日志</Title>
      <Card style={{ borderColor: '#2e3450' }}>
        <Space style={{ marginBottom: 16 }}>
          <Select
            value={filterAction}
            onChange={v => { setFilterAction(v); setPage(1); }}
            options={ACTION_OPTIONS}
            style={{ width: 140 }}
            placeholder="操作类型"
          />
          <Input.Search
            placeholder="按用户筛选"
            allowClear
            onSearch={v => { setFilterUser(v); setPage(1); }}
            style={{ width: 200 }}
          />
        </Space>
        <Table<AuditLogEntry>
          columns={columns}
          dataSource={items}
          rowKey={(_, i) => String(i)}
          loading={loading}
          size="small"
          pagination={{
            current: page,
            pageSize,
            total: Math.max(total, items.length),
            onChange: setPage,
            showTotal: t => `共 ${t} 条`,
          }}
        />
      </Card>
    </div>
  );
};

export default AuditLog;
