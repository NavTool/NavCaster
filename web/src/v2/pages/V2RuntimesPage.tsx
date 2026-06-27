import { Link } from 'react-router-dom';
import { ApartmentOutlined, ReloadOutlined, ThunderboltOutlined } from '@ant-design/icons';
import { Button, Select, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { v2AdminService } from '../api/adminService';
import type { DesiredRuntimeState, RuntimeSummary } from '../api/contracts';
import { V2ConfirmDialog } from '../components/V2ConfirmDialog';
import { V2MetricCard } from '../components/V2MetricCard';
import { V2StatusBadge } from '../components/V2StatusBadge';
import { V2TablePage } from '../components/V2TablePage';
import { formatDateTime, useV2Page } from './useV2Page';

export default function V2RuntimesPage() {
  const loader = useCallback((filters: Parameters<typeof v2AdminService.listRuntimes>[0]) => v2AdminService.listRuntimes(filters), []);
  const page = useV2Page<RuntimeSummary>(loader);
  const [target, setTarget] = useState<RuntimeSummary | null>(null);
  const [desiredState, setDesiredState] = useState<DesiredRuntimeState>('draining');

  async function submitDesiredState(reason: string) {
    if (!target) return;
    const receipt = await v2AdminService.setRuntimeDesiredState({ runtime_id: target.id, desired_state: desiredState, reason });
    message.success(receipt.message);
    setTarget(null);
  }

  const columns: ColumnsType<RuntimeSummary> = [
    {
      title: 'Runtime',
      dataIndex: 'name',
      fixed: 'left',
      render: (_, row) => (
        <div className="v2-primary-cell">
          <Link to={`/admin/control/runtimes/${row.id}`}>{row.name}</Link>
          <span>{row.id}</span>
        </div>
      ),
    },
    { title: 'Actual state', dataIndex: 'status', render: (status) => <V2StatusBadge status={status} /> },
    { title: 'Kind', dataIndex: 'kind' },
    { title: 'Host', dataIndex: 'host_name' },
    { title: 'Desired', dataIndex: 'desired_state' },
    { title: 'Current config', dataIndex: 'current_config_version_id' },
    { title: 'Target config', dataIndex: 'target_config_version_id' },
    { title: 'Workers', dataIndex: 'worker_count', align: 'right' },
    { title: 'Sessions', dataIndex: 'active_sessions', align: 'right' },
    { title: 'Pending intents', dataIndex: 'restart_intent_count', align: 'right' },
    { title: 'Updated', dataIndex: 'updated_at', render: formatDateTime },
    {
      title: 'Intent',
      fixed: 'right',
      render: (_, row) => <Button size="small" icon={<ThunderboltOutlined />} onClick={() => setTarget(row)}>Change state</Button>,
    },
  ];

  const running = page.items.filter((item) => item.status === 'running').length;
  const draining = page.items.filter((item) => item.status === 'draining').length;
  const failed = page.items.filter((item) => item.status === 'failed').length;

  return (
    <>
      <div className="v2-metric-grid">
        <V2MetricCard label="Visible running" value={running} detail="after current filters" icon={<ApartmentOutlined />} />
        <V2MetricCard label="Visible draining" value={draining} detail="operator intent in progress" />
        <V2MetricCard label="Visible failed" value={failed} detail="needs AdminService recovery" />
        <V2MetricCard label="Visible sessions" value={page.items.reduce((sum, item) => sum + item.active_sessions, 0)} detail="filtered total" />
      </div>
      <V2TablePage<RuntimeSummary>
        title="Runtimes"
        description="Runtime rows expose desired state and action intents. Web never starts, stops, or restarts a process directly."
        actions={<Button icon={<ReloadOutlined />} onClick={page.refresh}>Refresh</Button>}
        filters={page.filters}
        onFiltersChange={page.setFilters}
        columns={columns}
        data={page.items}
        total={page.total}
        loading={page.loading}
        rowKey="id"
      />
      <V2ConfirmDialog
        open={Boolean(target)}
        title="Submit runtime desired state"
        description={target ? `Create desired-state intent for ${target.name}.` : ''}
        intentLabel={`Set runtime desired state to ${desiredState}`}
        confirmText="Queue desired state"
        onCancel={() => setTarget(null)}
        onConfirm={submitDesiredState}
      />
      {target ? (
        <div className="v2-floating-intent">
          <span>Desired state</span>
          <Select<DesiredRuntimeState>
            value={desiredState}
            onChange={setDesiredState}
            options={[
              { label: 'Running', value: 'running' },
              { label: 'Draining', value: 'draining' },
              { label: 'Stopped', value: 'stopped' },
            ]}
          />
        </div>
      ) : null}
    </>
  );
}
