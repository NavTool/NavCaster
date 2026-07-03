import { ReloadOutlined, SettingOutlined } from '@ant-design/icons';
import { Button, Progress, Space } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { ControlPlaneOverview, HostSummary } from '../../api/contracts';
import { MetricCard } from '../components/MetricCard';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { formatDateTime, gb, useControlPage } from './useControlPage';

export default function HostsPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listHosts>[0]) => adminService.listHosts(filters), []);
  const page = useControlPage<HostSummary>(loader);
  const [overview, setOverview] = useState<ControlPlaneOverview | null>(null);

  useEffect(() => {
    void adminService.getOverview().then(setOverview).catch(() => setOverview(null));
  }, []);

  const columns: ColumnsType<HostSummary> = [
    {
      title: 'Host',
      dataIndex: 'name',
      fixed: 'left',
      render: (_, row) => (
        <div className="control-primary-cell">
          <strong>{row.name}</strong>
          <span>{row.id}</span>
        </div>
      ),
    },
    { title: 'Desired host', dataIndex: 'desired_state' },
    { title: 'Actual agent', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    {
      title: 'Convergence',
      dataIndex: 'convergence_status',
      render: (_, row) => (
        <div className="control-primary-cell">
          <ConvergenceBadge status={row.convergence_status} />
          <span>{row.convergence_detail}</span>
        </div>
      ),
    },
    { title: 'Region', dataIndex: 'region' },
    { title: 'Address', dataIndex: 'address' },
    { title: 'OS', dataIndex: 'os', render: (value) => value || '-' },
    { title: 'Arch', dataIndex: 'arch', render: (value) => value || '-' },
    { title: 'Runtimes', dataIndex: 'runtime_count', align: 'right' },
    { title: 'Workers', dataIndex: 'worker_count', align: 'right' },
    { title: 'CPU', dataIndex: 'cpu_load', width: 140, render: (value) => <Progress percent={Math.round(value)} size="small" strokeColor="#14b8a6" /> },
    { title: 'Memory', render: (_, row) => gb(row.memory_used_gb, row.memory_total_gb) },
    { title: 'Config', dataIndex: 'config_version_id' },
    { title: 'Last heartbeat', dataIndex: 'last_heartbeat_at', render: formatDateTime },
    { title: 'Last metric', dataIndex: 'last_metric_at', render: formatDateTime },
  ];

  return (
    <>
      <div className="control-metric-grid">
        <MetricCard label="Running hosts" value={overview?.running_hosts ?? '-'} detail="healthy heartbeat" />
        <MetricCard label="Offline hosts" value={overview?.offline_hosts ?? '-'} detail="requires operator review" />
        <MetricCard label="Running runtimes" value={overview?.running_runtimes ?? '-'} detail="AdminService observed" />
        <MetricCard label="Active sessions" value={overview?.active_sessions ?? '-'} detail="runtime reported" />
      </div>
      <TablePage<HostSummary>
        title="Hosts"
        description="Fleet inventory from AdminService live control APIs. Desired host state, agent heartbeat, last metric, and runtime convergence are displayed separately."
        actions={
          <Space>
            <Button icon={<ReloadOutlined />} onClick={page.refresh}>Refresh</Button>
            <Button type="primary" icon={<SettingOutlined />}>Prepare maintenance intent</Button>
          </Space>
        }
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
