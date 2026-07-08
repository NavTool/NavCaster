import { Link } from 'react-router-dom';
import { PauseCircleOutlined, PlayCircleOutlined, ReloadOutlined, ThunderboltOutlined } from '@ant-design/icons';
import { Alert, Button, Select, Space, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { DesiredRuntimeState, RuntimeActionIntent, RuntimeSummary } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { desiredRuntimeStateLabel, runtimeActionLabel, runtimeKindLabel } from '../labels';
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
    setLastIntent({ runtimeId: target.id, label: `期望状态：${desiredRuntimeStateLabel(desiredState)}`, message: receipt.message });
    setTarget(null);
    await page.refresh();
  }

  async function submitAction(reason: string) {
    if (!targetAction) return;
    const receipt = await adminService.submitRuntimeAction({ runtime_id: targetAction.runtime.id, action: targetAction.action, reason });
    message.success(receipt.message);
    setLastIntent({ runtimeId: targetAction.runtime.id, label: runtimeActionLabel(targetAction.action), message: receipt.message });
    setTargetAction(null);
    await page.refresh();
  }

  const columns: ColumnsType<RuntimeSummary> = [
    {
      title: '运行时',
      dataIndex: 'name',
      fixed: 'left',
      width: 240,
      render: (_, row) => (
        <div className="control-primary-cell">
          <Link to={`/admin/control/runtimes/${row.id}`}>{row.name}</Link>
          <span>{row.id}</span>
        </div>
      ),
    },
    { title: '期望状态', dataIndex: 'desired_state', render: desiredRuntimeStateLabel },
    { title: '实际状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    {
      title: '收敛状态',
      dataIndex: 'convergence_status',
      render: (_, row) => (
        <div className="control-primary-cell">
          <ConvergenceBadge status={row.convergence_status} />
          <span>{row.convergence_detail}</span>
        </div>
      ),
    },
    { title: '类型', dataIndex: 'kind', render: runtimeKindLabel },
    { title: '主机', dataIndex: 'host_name' },
    {
      title: '配置',
      render: (_, row) => (
        <div className="control-primary-cell">
          <strong>{row.target_config_version_id}</strong>
          <span>实际 {row.current_config_version_id}</span>
        </div>
      ),
    },
    {
      title: '工作线程',
      align: 'right',
      render: (_, row) => `${row.actual_worker_count} / ${row.desired_worker_count}`,
    },
    { title: '会话', dataIndex: 'active_sessions', align: 'right' },
    { title: '进程', dataIndex: 'process_id', align: 'right', render: (value) => value || '-' },
    {
      title: 'Redis',
      dataIndex: 'redis_connected',
      render: (value) => (value === undefined ? '-' : value ? '已连接' : '已断开'),
    },
    {
      title: '计数器',
      render: (_, row) => (
        <div className="control-primary-cell">
          <span>{`挂载点 ${row.mounts}`}</span>
          <span>{`源站 ${row.sources} / 客户端 ${row.clients}`}</span>
        </div>
      ),
    },
    { title: 'Loop p95', dataIndex: 'loop_delay_ms_p95', align: 'right', render: (value) => (value ? `${value} ms` : '-') },
    { title: '监听端口', dataIndex: 'listen_port', align: 'right', render: (value) => value || '-' },
    { title: '期望更新时间', dataIndex: 'desired_updated_at', render: formatDateTime },
    {
      title: '实际指标',
      render: (_, row) => (
        <div className="control-primary-cell">
          <span>{formatDateTime(row.last_metric_at)}</span>
          <span className={row.stale ? 'control-stale-text' : undefined}>{row.stale ? row.stale_detail : '新鲜'}</span>
        </div>
      ),
    },
    {
      title: '意图',
      fixed: 'right',
      width: 360,
      render: (_, row) => (
        <Space wrap size={4}>
          <Button size="small" icon={<PlayCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'start' })}>启动</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'stop' })}>停止</Button>
          <Button size="small" icon={<ReloadOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'restart' })}>重启</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'drain' })}>排空</Button>
          <Button size="small" icon={<ThunderboltOutlined />} onClick={() => setTarget(row)}>设置期望</Button>
        </Space>
      ),
    },
  ];

  const observedIntentRuntime = lastIntent ? page.items.find((item) => item.id === lastIntent.runtimeId) : undefined;

  return (
    <>
      {lastIntent ? (
        <Alert
          className="control-intent-alert"
          showIcon
          type={observedIntentRuntime?.convergence_status === 'converged' ? 'success' : 'warning'}
          message={observedIntentRuntime?.convergence_status === 'converged' ? '意图已观测为收敛' : '意图已接受，等待实际状态上报'}
          description={`${lastIntent.label}，运行时 ${lastIntent.runtimeId}。${observedIntentRuntime?.convergence_detail ?? lastIntent.message}`}
        />
      ) : null}
      <TablePage<RuntimeSummary>
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
      <ConfirmDialog
        open={Boolean(target)}
        title="提交运行时期望状态"
        description={target ? `为 ${target.name} 创建期望状态意图。` : ''}
        intentLabel={`将运行时期望状态设置为${desiredRuntimeStateLabel(desiredState)}`}
        confirmText="加入期望状态队列"
        onCancel={() => setTarget(null)}
        onConfirm={submitDesiredState}
      />
      <ConfirmDialog
        open={Boolean(targetAction)}
        title="提交运行时操作意图"
        description={targetAction ? `为 ${targetAction.runtime.name} 加入${runtimeActionLabel(targetAction.action)}意图。` : ''}
        intentLabel={targetAction ? `通过 AdminService 请求${runtimeActionLabel(targetAction.action)}` : ''}
        confirmText="加入操作意图队列"
        onCancel={() => setTargetAction(null)}
        onConfirm={submitAction}
      />
      {target ? (
        <div className="control-floating-intent">
          <span>期望状态</span>
          <Select<DesiredRuntimeState>
            value={desiredState}
            onChange={setDesiredState}
            options={[
              { label: '运行中', value: 'running' },
              { label: '排空中', value: 'draining' },
              { label: '已停止', value: 'stopped' },
            ]}
          />
        </div>
      ) : null}
    </>
  );
}
