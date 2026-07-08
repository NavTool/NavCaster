import { BranchesOutlined, ReloadOutlined } from '@ant-design/icons';
import { Button, Progress, Space, Tag, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { ConfigVersion } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { TablePage } from '../components/TablePage';
import { configStatusLabel } from '../labels';
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
      title: '版本',
      dataIndex: 'label',
      fixed: 'left',
      width: 220,
      render: (_, row) => <div className="control-primary-cell"><strong>{row.label}</strong><span>{row.id}</span></div>,
    },
    { title: '状态', dataIndex: 'status', render: (status: ConfigVersion['status']) => <Tag color={configStatusColor[status]}>{configStatusLabel(status)}</Tag> },
    {
      title: '发布状态',
      render: (_, row) => (
        <Progress
          percent={row.target_hosts ? Math.round((row.applied_hosts / row.target_hosts) * 100) : 0}
          size="small"
          format={() => `${row.applied_hosts}/${row.target_hosts}`}
        />
      ),
    },
    { title: '校验和', dataIndex: 'checksum' },
    { title: '创建人', dataIndex: 'created_by' },
    { title: '创建时间', dataIndex: 'created_at', render: formatDateTime },
    { title: '摘要', dataIndex: 'summary', width: 360 },
    {
      title: '意图',
      fixed: 'right',
      width: 120,
      render: (_, row) => <Button size="small" icon={<BranchesOutlined />} disabled={row.status === 'active'} onClick={() => setTarget(row)}>发布</Button>,
    },
  ];

  return (
    <>
      <TablePage<ConfigVersion>
        actions={
          <Space>
            <Button icon={<ReloadOutlined />} onClick={page.refresh}>刷新</Button>
            <Button type="primary" icon={<BranchesOutlined />}>新建草稿模板</Button>
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
        title="提交配置发布意图"
        description={target ? `通过 AdminService 将 ${target.label} 发布到全部主机。` : ''}
        intentLabel={target ? `发布 ${target.id} 到全部主机` : ''}
        confirmText="加入发布意图队列"
        onCancel={() => setTarget(null)}
        onConfirm={submitPublishIntent}
      />
    </>
  );
}
