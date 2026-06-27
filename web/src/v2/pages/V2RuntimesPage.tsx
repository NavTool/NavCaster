import { Link } from 'react-router-dom';
import { ApartmentOutlined, PauseCircleOutlined, PlayCircleOutlined, ReloadOutlined, ThunderboltOutlined } from '@ant-design/icons';
import { Button, Select, Space, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { v2AdminService } from '../api/adminService';
import type { DesiredRuntimeState, RuntimeActionIntent, RuntimeSummary } from '../api/contracts';
import { V2ConfirmDialog } from '../components/V2ConfirmDialog';
import { V2MetricCard } from '../components/V2MetricCard';
import { V2ConvergenceBadge, V2StatusBadge } from '../components/V2StatusBadge';
import { V2TablePage } from '../components/V2TablePage';
import { formatDateTime, useV2Page } from './useV2Page';

type RuntimeIntentAction = RuntimeActionIntent['action'];

export default function V2RuntimesPage() {
  const loader = useCallback((filters: Parameters<typeof v2AdminService.listRuntimes>[0]) => v2AdminService.listRuntimes(filters), []);
  const page = useV2Page<RuntimeSummary>(loader);
  const [target, setTarget] = useState<RuntimeSummary | null>(null);
  const [desiredState, setDesiredState] = useState<DesiredRuntimeState>('draining');
  const [targetAction, setTargetAction] = useState<{ runtime: RuntimeSummary; action: RuntimeIntentAction } | null>(null);

  async function submitDesiredState(reason: string) {
    if (!target) return;
    const receipt = await v2AdminService.setRuntimeDesiredState({ runtime_id: target.id, desired_state: desiredState, reason });
    message.success(receipt.message);
    setTarget(null);
    await page.refresh();
  }

  async function submitAction(reason: string) {
    if (!targetAction) return;
    const receipt = await v2AdminService.submitRuntimeAction({ runtime_id: targetAction.runtime.id, action: targetAction.action, reason, target_config_version_id: targetAction.runtime.target_config_version_id });
    message.success(receipt.message);
    setTargetAction(null);
    await page.refresh();
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
    { title: 'Desired', dataIndex: 'desired_state' },
    { title: 'Actual', dataIndex: 'status', render: (status) => <V2StatusBadge status={status} /> },
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
    { title: 'Kind', dataIndex: 'kind' },
    { title: 'Host', dataIndex: 'host_name' },
    {
      title: 'Config',
      render: (_, row) => (
        <div className="v2-primary-cell">
          <strong>{row.target_config_version_id}</strong>
          <span>actual {row.current_config_version_id}</span>
        </div>
      ),
    },
    {
      title: 'Workers',
      align: 'right',
      render: (_, row) => `${row.actual_worker_count} / ${row.desired_worker_count}`,
    },
    { title: 'Sessions', dataIndex: 'active_sessions', align: 'right' },
    { title: 'Loop p95', dataIndex: 'loop_delay_ms_p95', align: 'right', render: (value) => (value ? `${value} ms` : '-') },
    { title: 'Listen port', dataIndex: 'listen_port', align: 'right', render: (value) => value || '-' },
    { title: 'Desired update', dataIndex: 'desired_updated_at', render: formatDateTime },
    { title: 'Last metric', dataIndex: 'last_metric_at', render: formatDateTime },
    {
      title: 'Intent',
      fixed: 'right',
      render: (_, row) => (
        <Space.Compact>
          <Button size="small" icon={<PlayCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'restart' })}>Restart</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'drain-workers' })}>Drain</Button>
          <Button size="small" icon={<ThunderboltOutlined />} onClick={() => setTarget(row)}>Desired</Button>
        </Space.Compact>
      ),
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
        description="Runtime rows are read from AdminService live APIs. Desired state, actual state, last metric, and convergence are separate so intents are not shown as completed execution."
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
      <V2ConfirmDialog
        open={Boolean(targetAction)}
        title="Submit runtime action intent"
        description={targetAction ? `Queue ${targetAction.action} intent for ${targetAction.runtime.name}.` : ''}
        intentLabel={targetAction ? `Request ${targetAction.action} through AdminService` : ''}
        confirmText="Queue action intent"
        onCancel={() => setTargetAction(null)}
        onConfirm={submitAction}
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
