import { ReloadOutlined } from '@ant-design/icons';
import { Button, Progress } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback } from 'react';
import { adminService } from '../../api/adminService';
import type { WorkerMetric } from '../../api/contracts';
import { StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { formatDateTime, useControlPage } from './useControlPage';

export default function WorkersPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listWorkers>[0]) => adminService.listWorkers(filters), []);
  const page = useControlPage<WorkerMetric>(loader);

  const columns: ColumnsType<WorkerMetric> = [
    {
      title: '工作线程',
      dataIndex: 'name',
      fixed: 'left',
      width: 220,
      render: (_, row) => <div className="control-primary-cell"><strong>{row.name}</strong><span>{row.id}</span></div>,
    },
    { title: '状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '主机', dataIndex: 'host_name' },
    { title: '运行时', dataIndex: 'runtime_id' },
    { title: '挂载点', dataIndex: 'assigned_mount_points', align: 'right' },
    { title: '源站', render: (_, row) => Math.max(1, Math.round(row.assigned_mount_points / 4)), align: 'right' },
    { title: '客户端', dataIndex: 'active_sessions', align: 'right' },
    { title: 'Fanout/sec', dataIndex: 'throughput_kbps', render: (value) => `${Math.round(value / 8).toLocaleString()}` },
    { title: 'Fanout p95', dataIndex: 'latency_p95_ms', render: (value) => `${value} ms` },
    {
      title: 'Loop delay p95',
      dataIndex: 'error_rate',
      width: 160,
      render: (value) => <Progress percent={Math.min(Math.round(value), 100)} size="small" strokeColor={value > 5 ? '#f97316' : '#14b8a6'} />,
    },
    { title: 'Redis 发布', dataIndex: 'throughput_kbps', render: (value) => value.toLocaleString() },
    { title: '慢连接断开', dataIndex: 'error_rate', render: (value) => Math.round(value * 3) },
    { title: '更新时间', dataIndex: 'updated_at', render: formatDateTime },
  ];

  return (
    <>
      <TablePage<WorkerMetric>
        actions={<Button icon={<ReloadOutlined />} onClick={page.refresh}>刷新</Button>}
        filters={page.filters}
        onFiltersChange={page.setFilters}
        columns={columns}
        data={page.items}
        total={page.total}
        loading={page.loading}
        error={page.error}
        rowKey="id"
      />
    </>
  );
}
