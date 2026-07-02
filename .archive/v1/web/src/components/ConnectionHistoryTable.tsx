import React, { useEffect, useState } from 'react';
import { Table, Tag, Empty, Spin } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import type { ConnectionHistoryItem } from '../api';

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

interface Props {
  fetchData: () => Promise<ConnectionHistoryItem[]>;
  showMount?: boolean;  // show mount column (for user history)
  showAccount?: boolean; // show account column (for mount history)
}

const ConnectionHistoryTable: React.FC<Props> = ({ fetchData, showMount = false, showAccount = true }) => {
  const [data, setData] = useState<ConnectionHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let cancelled = false;
    const load = async () => {
      try {
        const result = await fetchData();
        if (!cancelled) setData(result);
      } catch {
        if (!cancelled) setData([]);
      } finally {
        if (!cancelled) setLoading(false);
      }
    };
    load();
    return () => { cancelled = true; };
  }, [fetchData]);

  const columns: ColumnsType<ConnectionHistoryItem & { key: string }> = [
    ...(showMount ? [{
      title: '挂载点', dataIndex: 'mount' as keyof ConnectionHistoryItem, key: 'mount', width: 120,
      render: (v: string) => <span style={{ fontFamily: 'monospace' }}>{v || '-'}</span>,
    } as ColumnsType<ConnectionHistoryItem & { key: string }>[number]] : []),
    ...(showAccount ? [{
      title: '账户', dataIndex: 'account' as keyof ConnectionHistoryItem, key: 'account', width: 100,
    } as ColumnsType<ConnectionHistoryItem & { key: string }>[number]] : []),
    {
      title: '类型', dataIndex: 'type', key: 'type', width: 80,
      render: (v: number) => {
        const t = typeLabels[v] || { text: `${v}`, color: 'default' };
        return <Tag color={t.color}>{t.text}</Tag>;
      },
    },
    {
      title: 'IP', dataIndex: 'host', key: 'host', width: 130,
      render: (v: string) => <span style={{ fontFamily: 'monospace', color: '#8b90a8' }}>{v || '-'}</span>,
    },
    {
      title: '节点', dataIndex: 'node_id', key: 'node_id', width: 80,
      render: (v: string) => <span style={{ color: '#8b90a8' }}>{v || '-'}</span>,
    },
    {
      title: '上线时间', dataIndex: 'connect_time', key: 'connect_time', width: 160,
      render: (v: number) => formatTimestamp(v),
    },
    {
      title: '下线时间', dataIndex: 'disconnect_time', key: 'disconnect_time', width: 160,
      render: (v: number) => v ? formatTimestamp(v) : <Tag color="green">在线</Tag>,
    },
    {
      title: '时长', dataIndex: 'duration', key: 'duration', width: 120,
      render: (v: number) => formatDuration(v),
    },
  ];

  if (loading) return <Spin />;
  if (data.length === 0) return <Empty description="暂无历史记录" />;

  return (
    <Table
      dataSource={data.map((item, idx) => ({ ...item, key: item.connect_key || String(idx) }))}
      columns={columns}
      size="small"
      pagination={{ pageSize: 10, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
      scroll={{ x: 800 }}
    />
  );
};

export default ConnectionHistoryTable;
