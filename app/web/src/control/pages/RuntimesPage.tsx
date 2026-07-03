import { Link } from 'react-router-dom';
import { ApartmentOutlined, PauseCircleOutlined, PlayCircleOutlined, ReloadOutlined, ThunderboltOutlined } from '@ant-design/icons';
import { Alert, Button, Select, Space, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { DesiredRuntimeState, RuntimeActionIntent, RuntimeSummary } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { MetricCard } from '../components/MetricCard';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { formatDateTime, useControlPage } from './useControlPage';

type RuntimeIntentAction = RuntimeActionIntent['action'];

export default function RuntimesPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listRuntimes>[0]) => adminService.listRuntimes(filters), []);
  const page = useControlPage<RuntimeSummary>(loader);
  const [target, setTarget] = useState<RuntimeSummary | null>(null);
  const [desiredState, setDesiredState] = useState<DesiredRuntimeState>('draining');
  const [targetAction, setTargetAction] = useState<{ runtime: RuntimeSummary; action: RuntimeIntentAction } | null>(null);
  const [lastIntent, setLastIntent] = useState<{ runtimeId: string; label: string; message: string } | null>(null);

  async function submitDesiredState(reason: string) {
    if (!target) return;
    const receipt = await adminService.setRuntimeDesiredState({ runtime_id: target.id, desired_state: desiredState, reason });
    message.success(receipt.message);
    setLastIntent({ runtimeId: target.id, label: `desired ${desiredState}`, message: receipt.message });
    setTarget(null);
    await page.refresh();
  }

  async function submitAction(reason: string) {
    if (!targetAction) return;
    const receipt = await adminService.submitRuntimeAction({ runtime_id: targetAction.runtime.id, action: targetAction.action, reason });
    message.success(receipt.message);
    setLastIntent({ runtimeId: targetAction.runtime.id, label: targetAction.action, message: receipt.message });
    setTargetAction(null);
    await page.refresh();
  }

  const columns: ColumnsType<RuntimeSummary> = [
    {
      title: 'Runtime',
      dataIndex: 'name',
      fixed: 'left',
      render: (_, row) => (
        <div className="control-primary-cell">
          <Link to={`/admin/control/runtimes/${row.id}`}>{row.name}</Link>
          <span>{row.id}</span>
        </div>
      ),
    },
    { title: 'Desired', dataIndex: 'desired_state' },
    { title: 'Actual', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
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
    { title: 'Kind', dataIndex: 'kind' },
    { title: 'Host', dataIndex: 'host_name' },
    {
      title: 'Config',
      render: (_, row) => (
        <div className="control-primary-cell">
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
    { title: 'Proc', dataIndex: 'process_id', align: 'right', render: (value) => value || '-' },
    {
      title: 'Redis',
      dataIndex: 'redis_connected',
      render: (value) => (value === undefined ? '-' : value ? 'connected' : 'disconnected'),
    },
    {
      title: 'Counters',
      render: (_, row) => (
        <div className="control-primary-cell">
          <span>{`mounts ${row.mounts}`}</span>
          <span>{`sources ${row.sources} / clients ${row.clients}`}</span>
        </div>
      ),
    },
    { title: 'Loop p95', dataIndex: 'loop_delay_ms_p95', align: 'right', render: (value) => (value ? `${value} ms` : '-') },
    { title: 'Listen port', dataIndex: 'listen_port', align: 'right', render: (value) => value || '-' },
    { title: 'Desired update', dataIndex: 'desired_updated_at', render: formatDateTime },
    {
      title: 'Actual metric',
      render: (_, row) => (
        <div className="control-primary-cell">
          <span>{formatDateTime(row.last_metric_at)}</span>
          <span className={row.stale ? 'control-stale-text' : undefined}>{row.stale ? row.stale_detail : 'fresh'}</span>
        </div>
      ),
    },
    {
      title: 'Intent',
      fixed: 'right',
      render: (_, row) => (
        <Space wrap size={4}>
          <Button size="small" icon={<PlayCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'start' })}>Start</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'stop' })}>Stop</Button>
          <Button size="small" icon={<ReloadOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'restart' })}>Restart</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'drain' })}>Drain</Button>
          <Button size="small" icon={<ThunderboltOutlined />} onClick={() => setTarget(row)}>Desired</Button>
        </Space>
      ),
    },
  ];

  const running = page.items.filter((item) => item.status === 'running').length;
  const draining = page.items.filter((item) => item.status === 'draining').length;
  const failed = page.items.filter((item) => item.status === 'failed').length;
  const pending = page.items.filter((item) => item.convergence_status === 'pending').length;
  const stale = page.items.filter((item) => item.stale).length;
  const observedIntentRuntime = lastIntent ? page.items.find((item) => item.id === lastIntent.runtimeId) : undefined;

  return (
    <>
      <div className="control-metric-grid">
        <MetricCard label="Visible running" value={running} detail="after current filters" icon={<ApartmentOutlined />} />
        <MetricCard label="Visible draining" value={draining} detail="operator intent in progress" />
        <MetricCard label="Visible failed" value={failed} detail="needs AdminService recovery" />
        <MetricCard label="Pending / stale" value={`${pending} / ${stale}`} detail="convergence / actual metrics" />
      </div>
      {lastIntent ? (
        <Alert
          className="control-intent-alert"
          showIcon
          type={observedIntentRuntime?.convergence_status === 'converged' ? 'success' : 'warning'}
          message={observedIntentRuntime?.convergence_status === 'converged' ? 'Intent observed as converged' : 'Intent accepted; waiting for observed actual state'}
          description={`${lastIntent.label} for ${lastIntent.runtimeId}. ${observedIntentRuntime?.convergence_detail ?? lastIntent.message}`}
        />
      ) : null}
      <TablePage<RuntimeSummary>
        title="Runtimes"
        description="Runtime rows are read from AdminService live APIs. Desired state, actual state, last metric, and convergence are separate so intents are not shown as completed execution."
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
      <ConfirmDialog
        open={Boolean(target)}
        title="Submit runtime desired state"
        description={target ? `Create desired-state intent for ${target.name}.` : ''}
        intentLabel={`Set runtime desired state to ${desiredState}`}
        confirmText="Queue desired state"
        onCancel={() => setTarget(null)}
        onConfirm={submitDesiredState}
      />
      <ConfirmDialog
        open={Boolean(targetAction)}
        title="Submit runtime action intent"
        description={targetAction ? `Queue ${targetAction.action} intent for ${targetAction.runtime.name}.` : ''}
        intentLabel={targetAction ? `Request ${targetAction.action} through AdminService` : ''}
        confirmText="Queue action intent"
        onCancel={() => setTargetAction(null)}
        onConfirm={submitAction}
      />
      {target ? (
        <div className="control-floating-intent">
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
