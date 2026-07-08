import { Link } from 'react-router-dom';
import { PauseCircleOutlined, PlayCircleOutlined, ReloadOutlined, ThunderboltOutlined } from '@ant-design/icons';
import { Alert, Button, Space, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { RuntimeActionIntent, RuntimeSummary } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { TablePage } from '../components/TablePage';
import { desiredRuntimeStateLabel, runtimeActionLabel, runtimeKindLabel } from '../labels';
import { defaultFilters, formatDateTime, useControlPage } from './useControlPage';

type RuntimeIntentAction = RuntimeActionIntent['action'];

function formatBps(value: number) {
  if (value >= 1_000_000) return `${(value / 1_000_000).toFixed(1)} Mbps`;
  if (value >= 1_000) return `${(value / 1_000).toFixed(1)} Kbps`;
  return `${Math.round(value)} bps`;
}

export default function CasterNodesPage() {
  const loader = useCallback((filters: Parameters<typeof adminService.listRuntimes>[0]) => adminService.listRuntimes(filters), []);
  const page = useControlPage<RuntimeSummary>(loader, { ...defaultFilters, status: 'running' });
  const [targetAction, setTargetAction] = useState<{ runtime: RuntimeSummary; action: RuntimeIntentAction } | null>(null);
  const [lastIntent, setLastIntent] = useState<{ runtimeId: string; label: string; message: string } | null>(null);

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
      title: 'Caster 节点',
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
    { title: '节点状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '期望状态', dataIndex: 'desired_state', render: desiredRuntimeStateLabel },
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
    { title: '类型', dataIndex: 'kind', render: runtimeKindLabel },
    { title: '所属实例', dataIndex: 'host_name', render: (_, row) => <Link to={`/admin/control/instances/${row.host_id}`}>{row.host_name || row.host_id}</Link> },
    { title: '监听端口', dataIndex: 'listen_port', align: 'right', render: (value) => value || '-' },
    { title: '进程', dataIndex: 'process_id', align: 'right', render: (value) => value || '-' },
    { title: '会话', dataIndex: 'active_sessions', align: 'right' },
    { title: '吞吐', render: (_, row) => formatBps(row.send_bps + row.recv_bps) },
    { title: '源站 / 客户端', render: (_, row) => `${row.sources} / ${row.clients}` },
    { title: '工作线程', render: (_, row) => `${row.actual_worker_count} / ${row.desired_worker_count || row.actual_worker_count}` },
    { title: 'Redis', dataIndex: 'redis_connected', render: (value) => (value === undefined ? '-' : value ? '已连接' : '已断开') },
    { title: '最近指标', dataIndex: 'last_metric_at', render: formatDateTime },
    {
      title: '操作',
      fixed: 'right',
      width: 260,
      render: (_, row) => (
        <Space wrap size={4}>
          <Button size="small" icon={<PlayCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'start' })}>启动</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'stop' })}>停止</Button>
          <Button size="small" icon={<ReloadOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'restart' })}>重启</Button>
          <Button size="small" icon={<ThunderboltOutlined />} onClick={() => setTargetAction({ runtime: row, action: row.desired_state === 'draining' ? 'undrain' : 'drain' })}>
            {row.desired_state === 'draining' ? '恢复' : '排空'}
          </Button>
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
          message={observedIntentRuntime?.convergence_status === 'converged' ? '操作意图已收敛' : '操作意图已提交'}
          description={`${lastIntent.label}，节点 ${lastIntent.runtimeId}。${observedIntentRuntime?.convergence_detail ?? lastIntent.message}`}
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
        open={Boolean(targetAction)}
        title="提交节点操作意图"
        description={targetAction ? `为 ${targetAction.runtime.name} 加入${runtimeActionLabel(targetAction.action)}意图。` : ''}
        intentLabel={targetAction ? `通过 AdminService 请求${runtimeActionLabel(targetAction.action)}` : ''}
        confirmText="加入操作意图队列"
        onCancel={() => setTargetAction(null)}
        onConfirm={submitAction}
      />
    </>
  );
}
