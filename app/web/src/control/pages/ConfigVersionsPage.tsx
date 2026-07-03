import { BranchesOutlined, ReloadOutlined } from '@ant-design/icons';
import { Button, Progress, Space, Tag, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { ConfigVersion } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { MetricCard } from '../components/MetricCard';
import { TablePage } from '../components/TablePage';
import { formatDateTime, useControlPage } from './useControlPage';

const configStatusColor: Record<ConfigVersion['status'], string> = {
  draft: 'default',
  active: 'green',
  superseded: 'blue',
  failed: 'red',
};

export default function ConfigVersionsPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listConfigVersions>[0]) => adminService.listConfigVersions(filters), []);
  const page = useControlPage<ConfigVersion>(loader);
  const [target, setTarget] = useState<ConfigVersion | null>(null);

  async function submitPublishIntent(reason: string) {
    if (!target) return;
    const receipt = await adminService.publishConfigVersion({ config_version_id: target.id, target_scope: 'all-hosts', reason });
    message.success(receipt.message);
    setTarget(null);
  }

  const columns: ColumnsType<ConfigVersion> = [
    {
      title: 'Version',
      dataIndex: 'label',
      fixed: 'left',
      render: (_, row) => <div className="control-primary-cell"><strong>{row.label}</strong><span>{row.id}</span></div>,
    },
    { title: 'Status', dataIndex: 'status', render: (status: ConfigVersion['status']) => <Tag color={configStatusColor[status]}>{status}</Tag> },
    {
      title: 'Release status',
      render: (_, row) => (
        <Progress
          percent={row.target_hosts ? Math.round((row.applied_hosts / row.target_hosts) * 100) : 0}
          size="small"
          format={() => `${row.applied_hosts}/${row.target_hosts}`}
        />
      ),
    },
    { title: 'Checksum', dataIndex: 'checksum' },
    { title: 'Created by', dataIndex: 'created_by' },
    { title: 'Created', dataIndex: 'created_at', render: formatDateTime },
    { title: 'Summary', dataIndex: 'summary', width: 360 },
    {
      title: 'Intent',
      fixed: 'right',
      render: (_, row) => <Button size="small" icon={<BranchesOutlined />} disabled={row.status === 'active'} onClick={() => setTarget(row)}>Publish</Button>,
    },
  ];

  const active = page.items.filter((item) => item.status === 'active').length;
  const failed = page.items.filter((item) => item.status === 'failed').length;
  const targetHosts = page.items.reduce((sum, item) => sum + item.target_hosts, 0);
  const appliedHosts = page.items.reduce((sum, item) => sum + item.applied_hosts, 0);

  return (
    <>
      <div className="control-metric-grid">
        <MetricCard label="Visible versions" value={page.items.length} detail="after filters" />
        <MetricCard label="Active versions" value={active} detail="should converge to one" />
        <MetricCard label="Failed versions" value={failed} detail="rollout rejected" />
        <MetricCard label="Applied hosts" value={`${appliedHosts}/${targetHosts}`} detail="visible version sum" />
      </div>
      <TablePage<ConfigVersion>
        title="系统设置"
        description="配置发布以控制面意图提交，Web 不直接写入 Redis、PostgreSQL 或本地文件。"
        actions={
          <Space>
            <Button icon={<ReloadOutlined />} onClick={page.refresh}>Refresh</Button>
            <Button type="primary" icon={<BranchesOutlined />}>New draft skeleton</Button>
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
      <ConfirmDialog
        open={Boolean(target)}
        title="Submit config publish intent"
        description={target ? `Publish ${target.label} to all hosts through AdminService.` : ''}
        intentLabel={target ? `Publish ${target.id} to all-hosts` : ''}
        confirmText="Queue publish intent"
        onCancel={() => setTarget(null)}
        onConfirm={submitPublishIntent}
      />
    </>
  );
}
