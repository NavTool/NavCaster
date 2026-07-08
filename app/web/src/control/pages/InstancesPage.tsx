import { Link } from 'react-router-dom';
import { ReloadOutlined } from '@ant-design/icons';
import { Button, Progress } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback } from 'react';
import { adminService } from '../../api/adminService';
import type { HostSummary } from '../../api/contracts';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { hostDesiredStateLabel } from '../labels';
import { formatDateTime, gb, useControlPage } from './useControlPage';

export default function InstancesPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listHosts>[0]) => adminService.listHosts(filters), []);
  const page = useControlPage<HostSummary>(loader);

  const columns: ColumnsType<HostSummary> = [
    {
      title: '实例',
      dataIndex: 'name',
      fixed: 'left',
      width: 240,
      render: (_, row) => (
        <div className="control-primary-cell">
          <Link to={`/admin/control/instances/${row.id}`}>{row.name}</Link>
          <span>{row.id}</span>
        </div>
      ),
    },
    { title: 'Agent', dataIndex: 'agent_id', render: (value) => value || '-' },
    { title: '期望状态', dataIndex: 'desired_state', render: hostDesiredStateLabel },
    { title: '实例状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    {
      title: '收敛',
      dataIndex: 'convergence_status',
      render: (_, row) => (
        <div className="control-primary-cell">
          <ConvergenceBadge status={row.convergence_status} />
          <span>{row.convergence_detail}</span>
        </div>
      ),
    },
    { title: '区域', dataIndex: 'region' },
    { title: '地址', dataIndex: 'address' },
    { title: '系统', render: (_, row) => [row.os, row.arch].filter(Boolean).join(' / ') || '-' },
    { title: 'Caster 节点', dataIndex: 'runtime_count', align: 'right' },
    { title: '工作线程', dataIndex: 'worker_count', align: 'right' },
    { title: 'CPU', dataIndex: 'cpu_load', width: 150, render: (value) => <Progress percent={Math.round(value || 0)} size="small" strokeColor="#14b8a6" /> },
    { title: '内存', render: (_, row) => gb(row.memory_used_gb, row.memory_total_gb) },
    { title: '最近心跳', dataIndex: 'last_heartbeat_at', render: formatDateTime },
    { title: '最近指标', dataIndex: 'last_metric_at', render: formatDateTime },
  ];

  return (
    <>
      <TablePage<HostSummary>
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
