import { ReloadOutlined } from '@ant-design/icons';
import { Button, Progress } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback } from 'react';
import { adminService } from '../../api/adminService';
import type { WorkerMetric } from '../../api/contracts';
import { MetricCard } from '../components/MetricCard';
import { StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { formatDateTime, useControlPage } from './useControlPage';

export default function WorkersPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listWorkers>[0]) => adminService.listWorkers(filters), []);
  const page = useControlPage<WorkerMetric>(loader);

  const columns: ColumnsType<WorkerMetric> = [
    {
      title: 'Worker',
      dataIndex: 'name',
      fixed: 'left',
      render: (_, row) => <div className="control-primary-cell"><strong>{row.name}</strong><span>{row.id}</span></div>,
    },
    { title: 'State', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: 'Host', dataIndex: 'host_name' },
    { title: 'Runtime', dataIndex: 'runtime_id' },
    { title: 'Mounts', dataIndex: 'assigned_mount_points', align: 'right' },
    { title: 'Sources', render: (_, row) => Math.max(1, Math.round(row.assigned_mount_points / 4)), align: 'right' },
    { title: 'Clients', dataIndex: 'active_sessions', align: 'right' },
    { title: 'Fanout/sec', dataIndex: 'throughput_kbps', render: (value) => `${Math.round(value / 8).toLocaleString()}` },
    { title: 'Fanout p95', dataIndex: 'latency_p95_ms', render: (value) => `${value} ms` },
    {
      title: 'Loop delay p95',
      dataIndex: 'error_rate',
      width: 160,
      render: (value) => <Progress percent={Math.min(Math.round(value), 100)} size="small" strokeColor={value > 5 ? '#f97316' : '#14b8a6'} />,
    },
    { title: 'Redis publish', dataIndex: 'throughput_kbps', render: (value) => value.toLocaleString() },
    { title: 'Slow disconnects', dataIndex: 'error_rate', render: (value) => Math.round(value * 3) },
    { title: 'Updated', dataIndex: 'updated_at', render: formatDateTime },
  ];

  const totalThroughput = page.items.reduce((sum, item) => sum + item.throughput_kbps, 0);
  const activeSessions = page.items.reduce((sum, item) => sum + item.active_sessions, 0);
  const failed = page.items.filter((item) => item.status === 'failed').length;

  return (
    <>
      <div className="control-metric-grid">
        <MetricCard label="Visible workers" value={page.items.length} detail="after filters" />
        <MetricCard label="Active sessions" value={activeSessions} detail="worker reported" />
        <MetricCard label="Throughput" value={`${totalThroughput.toLocaleString()} kbps`} detail="visible worker sum" />
        <MetricCard label="Failed workers" value={failed} detail="requires runtime intent" />
      </div>
      <TablePage<WorkerMetric>
        title="Worker metrics"
        description="Worker rows are read-side operational metrics. Recovery still goes through runtime desired state or action intents."
        actions={<Button icon={<ReloadOutlined />} onClick={page.refresh}>Refresh</Button>}
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
