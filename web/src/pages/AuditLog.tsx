import React, { useEffect, useState, useCallback } from 'react';
import { Card, Input, Select, Tag, Space, Button, message, Typography } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import PageContainer from '../components/PageContainer';
import DataTable from '../components/DataTable';
import { getAuditLog } from '../api';
import type { AuditEntry } from '../api/types';

const AuditLog: React.FC = () => {
  const [items, setItems] = useState<AuditEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [actor, setActor] = useState('');
  const [action, setAction] = useState('');
  const [target, setTarget] = useState('');
  const [limit, setLimit] = useState(200);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await getAuditLog({ limit, actor: actor || undefined, action: action || undefined, target: target || undefined });
      setItems(res.items || []);
    } catch (e) {
      message.error('加载审计日志失败');
    } finally {
      setLoading(false);
    }
  }, [actor, action, target, limit]);

  useEffect(() => { refresh(); }, [refresh]);

  const columns = [
    { title: 'ID', dataIndex: 'id', width: 80, sorter: (a: AuditEntry, b: AuditEntry) => a.id - b.id },
    { title: '时间', dataIndex: 'timestamp', width: 170,
      render: (t: number) => t ? new Date(t * 1000).toLocaleString() : '-' },
    { title: '用户', dataIndex: 'actor', width: 120 },
    { title: 'IP', dataIndex: 'source_ip', width: 130 },
    { title: '节点', dataIndex: 'node_id', width: 160, ellipsis: true },
    { title: '操作', dataIndex: 'action', ellipsis: true,
      render: (a: string) => <Typography.Text code style={{ fontSize: 12 }}>{a}</Typography.Text> },
    { title: '资源', dataIndex: 'target_type', width: 100,
      render: (t: string) => t ? <Tag color="geekblue">{t}</Tag> : '-' },
    { title: '资源ID', dataIndex: 'target_id', width: 140, ellipsis: true },
    { title: '结果', dataIndex: 'result', width: 80,
      render: (r: number) => <Tag color={r < 300 ? 'green' : r < 400 ? 'gold' : 'red'}>{r}</Tag> },
    { title: 'Payload', dataIndex: 'payload', ellipsis: true, width: 280,
      render: (p: string) => p ? <Typography.Text code style={{ fontSize: 11 }}>{p.length > 80 ? p.slice(0, 80) + '…' : p}</Typography.Text> : '-' },
  ];

  return (
    <PageContainer title="审计日志" subtitle="所有 POST/PUT/DELETE 请求都会被记录到 Redis (LOG:AUDIT) 并保留最近 50000 条。">
      <Card style={{ marginBottom: 12 }}>
        <Space wrap>
          <Input placeholder="用户名" value={actor} onChange={e => setActor(e.target.value)} style={{ width: 140 }} allowClear />
          <Input placeholder="操作 (action 子串)" value={action} onChange={e => setAction(e.target.value)} style={{ width: 200 }} allowClear />
          <Input placeholder="资源类型" value={target} onChange={e => setTarget(e.target.value)} style={{ width: 140 }} allowClear />
          <Select value={limit} onChange={setLimit} style={{ width: 120 }}
            options={[100, 200, 500, 1000].map(n => ({ value: n, label: `最近 ${n}` }))} />
          <Button icon={<ReloadOutlined />} onClick={refresh} loading={loading}>刷新</Button>
        </Space>
      </Card>
      <DataTable
        rowKey="id"
        columns={columns as any}
        dataSource={items}
        loading={loading}
        size="small"
        pagination={{ pageSize: 50, showSizeChanger: true }}
      />
    </PageContainer>
  );
};

export default AuditLog;
