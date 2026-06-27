import { ReloadOutlined, SettingOutlined } from '@ant-design/icons';
import { Button, Progress, Space } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { v2AdminService } from '../api/adminService';
import type { ControlPlaneOverview, HostSummary } from '../api/contracts';
import { V2MetricCard } from '../components/V2MetricCard';
import { V2ConvergenceBadge, V2StatusBadge } from '../components/V2StatusBadge';
import { V2TablePage } from '../components/V2TablePage';
import { formatDateTime, gb, useV2Page } from './useV2Page';

export default function V2HostsPage() {
  const loader = useCallback((filters: Parameters<typeof v2AdminService.listHosts>[0]) => v2AdminService.listHosts(filters), []);
  const page = useV2Page<HostSummary>(loader);
  const [overview, setOverview] = useState<ControlPlaneOverview | null>(null);

  useEffect(() => {
    void v2AdminService.getOverview().then(setOverview);
  }, []);

  const columns: ColumnsType<HostSummary> = [
    {
      title: 'Host',
      dataIndex: 'name',
      fixed: 'left',
      render: (_, row) => (
        <div className="v2-primary-cell">
          <strong>{row.name}</strong>
          <span>{row.id}</span>
        </div>
      ),
    },
    { title: 'Desired host', dataIndex: 'desired_state' },
    { title: 'Actual agent', dataIndex: 'status', render: (status) => <V2StatusBadge status={status} /> },
    {
      title: 'Convergence',
      dataIndex: 'convergence_status',
      render: (_, row) => (
        <div className="v2-primary-cell">
          <V2ConvergenceBadge status={row.convergence_status} />
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
    { title: 'CPU', dataIndex: 'cpu_load', width: 140, render: (value) => <Progress percent={Math.round(value)} size="small" strokeColor="#2f7d62" /> },
    { title: 'Memory', render: (_, row) => gb(row.memory_used_gb, row.memory_total_gb) },
    { title: 'Config', dataIndex: 'config_version_id' },
    { title: 'Last heartbeat', dataIndex: 'last_heartbeat_at', render: formatDateTime },
    { title: 'Last metric', dataIndex: 'last_metric_at', render: formatDateTime },
  ];

  return (
    <>
      <div className="v2-metric-grid">
        <V2MetricCard label="Running hosts" value={overview?.running_hosts ?? '-'} detail="healthy heartbeat" />
        <V2MetricCard label="Offline hosts" value={overview?.offline_hosts ?? '-'} detail="requires operator review" />
        <V2MetricCard label="Running runtimes" value={overview?.running_runtimes ?? '-'} detail="AdminService observed" />
        <V2MetricCard label="Active sessions" value={overview?.active_sessions ?? '-'} detail="runtime reported" />
      </div>
      <V2TablePage<HostSummary>
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
        rowKey="id"
      />
    </>
  );
}
