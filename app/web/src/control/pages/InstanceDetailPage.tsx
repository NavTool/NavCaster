import { Link, useNavigate, useParams } from 'react-router-dom';
import {
  ArrowLeftOutlined,
  PauseCircleOutlined,
  PlayCircleOutlined,
  PlusOutlined,
  ReloadOutlined,
  SettingOutlined,
} from '@ant-design/icons';
import { Alert, Button, Descriptions, Empty, Form, Input, InputNumber, Modal, Progress, Select, Space, Table, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { DesiredRuntimeState, HostSummary, RuntimeActionIntent, RuntimeRestartPolicy, RuntimeSummary } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { desiredRuntimeStateLabel, runtimeActionLabel, runtimeKindLabel } from '../labels';
import { formatDateTime, gb } from './useControlPage';

type RuntimeIntentAction = Extract<RuntimeActionIntent['action'], 'start' | 'stop' | 'restart' | 'drain' | 'undrain'>;

type RuntimeFormValues = {
  name: string;
  desired_state: DesiredRuntimeState;
  config_version: number;
  listen_port: number;
  worker_count: number;
  max_worker_count: number;
  restart_policy: RuntimeRestartPolicy;
  start_immediately: boolean;
  draining: boolean;
};

function numberValue(value: number | null | undefined, fallback: number) {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function restartPolicyLabel(value: RuntimeRestartPolicy) {
  if (value === 'always') return '始终重启';
  if (value === 'on_failure') return '失败时重启';
  return '不自动重启';
}

export default function InstanceDetailPage() {
  const { id = '' } = useParams();
  const navigate = useNavigate();
  const [host, setHost] = useState<HostSummary | null>(null);
  const [runtimes, setRuntimes] = useState<RuntimeSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [createOpen, setCreateOpen] = useState(false);
  const [editingRuntime, setEditingRuntime] = useState<RuntimeSummary | null>(null);
  const [targetAction, setTargetAction] = useState<{ runtime: RuntimeSummary; action: RuntimeIntentAction } | null>(null);
  const [createForm] = Form.useForm<RuntimeFormValues>();
  const [configForm] = Form.useForm<RuntimeFormValues>();

  const refresh = useCallback(async () => {
    if (!id) return;
    setLoading(true);
    try {
      const [hostPage, runtimePage] = await Promise.all([
        adminService.listHosts({ page: 1, pageSize: 500, status: 'all' }),
        adminService.listRuntimes({ page: 1, pageSize: 500, status: 'all', hostId: id }),
      ]);
      setHost(hostPage.items.find((row) => row.id === id) ?? null);
      setRuntimes(runtimePage.items);
      setError('');
    } catch (err) {
      setHost(null);
      setRuntimes([]);
      setError(err instanceof Error ? err.message : '实例数据加载失败');
    } finally {
      setLoading(false);
    }
  }, [id]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  function openCreate() {
    createForm.setFieldsValue({
      name: `caster-${runtimes.length + 1}`,
      desired_state: 'running',
      config_version: 1,
      listen_port: 2101 + runtimes.length,
      worker_count: 2,
      max_worker_count: 4,
      restart_policy: 'always',
      start_immediately: true,
    });
    setCreateOpen(true);
  }

  function openConfig(row: RuntimeSummary) {
    configForm.setFieldsValue({
      name: row.name,
      desired_state: row.desired_state,
      config_version: Number(row.target_config_version_id.replace('cfg-', '')) || 1,
      listen_port: row.listen_port || 2101,
      worker_count: row.desired_worker_count || row.actual_worker_count || 1,
      max_worker_count: Math.max(row.desired_worker_count || row.actual_worker_count || 1, 1),
      restart_policy: 'always',
      draining: row.desired_state === 'draining',
      start_immediately: row.desired_state === 'running',
    });
    setEditingRuntime(row);
  }

  async function submitCreate(values: RuntimeFormValues) {
    if (!id) return;
    try {
      await adminService.createRuntime({
        host_id: id,
        name: values.name.trim(),
        desired_state: values.desired_state,
        config_version: numberValue(values.config_version, 1),
        listen_port: numberValue(values.listen_port, 2101),
        worker_count: numberValue(values.worker_count, 1),
        max_worker_count: numberValue(values.max_worker_count, numberValue(values.worker_count, 1)),
        restart_policy: values.restart_policy,
        start_immediately: Boolean(values.start_immediately),
      });
      message.success('Caster 节点创建意图已提交');
      setCreateOpen(false);
      await refresh();
    } catch (err) {
      message.error(err instanceof Error ? err.message : '创建 Caster 节点失败');
    }
  }

  async function submitConfig(values: RuntimeFormValues) {
    if (!editingRuntime) return;
    try {
      await adminService.updateRuntimeConfig({
        runtime_id: editingRuntime.id,
        desired_state: values.desired_state,
        config_version: numberValue(values.config_version, 1),
        listen_port: numberValue(values.listen_port, editingRuntime.listen_port || 2101),
        worker_count: numberValue(values.worker_count, editingRuntime.desired_worker_count || 1),
        max_worker_count: numberValue(values.max_worker_count, values.worker_count || 1),
        restart_policy: values.restart_policy,
        draining: Boolean(values.draining),
      });
      message.success('Caster 节点期望配置已更新');
      setEditingRuntime(null);
      await refresh();
    } catch (err) {
      message.error(err instanceof Error ? err.message : '更新 Caster 节点配置失败');
    }
  }

  async function submitAction(reason: string) {
    if (!targetAction) return;
    try {
      await adminService.submitRuntimeAction({ runtime_id: targetAction.runtime.id, action: targetAction.action, reason });
      message.success(`${runtimeActionLabel(targetAction.action)}意图已提交`);
      setTargetAction(null);
      await refresh();
    } catch (err) {
      message.error(err instanceof Error ? err.message : '提交节点操作失败');
    }
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
    { title: '状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '期望', dataIndex: 'desired_state', render: desiredRuntimeStateLabel },
    {
      title: '收敛',
      render: (_, row) => (
        <div className="control-primary-cell">
          <ConvergenceBadge status={row.convergence_status} />
          <span>{row.convergence_detail}</span>
        </div>
      ),
    },
    { title: '类型', dataIndex: 'kind', render: runtimeKindLabel },
    { title: '监听端口', dataIndex: 'listen_port', align: 'right', render: (value) => value || '-' },
    { title: '工作线程', render: (_, row) => `${row.actual_worker_count} / ${row.desired_worker_count || row.actual_worker_count}` },
    { title: '会话', dataIndex: 'active_sessions', align: 'right' },
    { title: '最近指标', dataIndex: 'last_metric_at', render: formatDateTime },
    {
      title: '操作',
      fixed: 'right',
      width: 280,
      render: (_, row) => (
        <Space wrap size={4}>
          <Button size="small" icon={<SettingOutlined />} onClick={() => openConfig(row)}>配置</Button>
          <Button size="small" icon={<PlayCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'start' })}>启动</Button>
          <Button size="small" icon={<PauseCircleOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'stop' })}>停止</Button>
          <Button size="small" icon={<ReloadOutlined />} onClick={() => setTargetAction({ runtime: row, action: 'restart' })}>重启</Button>
        </Space>
      ),
    },
  ];

  return (
    <div className="control-detail-page node-detail-page">
      <section className="control-detail-toolbar control-table-toolbar">
        <Button className="node-back-button" type="text" icon={<ArrowLeftOutlined />} onClick={() => navigate('/admin/control/instances')}>返回实例管理</Button>
        <Space className="control-table-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
          <Button type="primary" icon={<PlusOutlined />} onClick={openCreate}>新增 Caster</Button>
        </Space>
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="实例数据不可用" description={error} /> : null}

      <section className="control-panel node-detail-panel">
        <h2>实例资源</h2>
        {host ? (
          <div className="node-resource-grid">
            <div>
              <span>CPU</span>
              <Progress percent={Math.round(host.cpu_load || 0)} strokeColor="#14b8a6" />
            </div>
            <div>
              <span>内存</span>
              <strong>{gb(host.memory_used_gb, host.memory_total_gb)}</strong>
            </div>
            <div>
              <span>系统</span>
              <strong>{[host.os, host.arch].filter(Boolean).join(' / ') || '-'}</strong>
            </div>
            <div>
              <span>心跳</span>
              <strong>{formatDateTime(host.last_heartbeat_at)}</strong>
            </div>
          </div>
        ) : (
          <Empty description={loading ? '正在加载实例信息' : '没有找到该实例'} />
        )}
        {host ? (
          <Descriptions className="node-descriptions" size="small" column={2}>
            <Descriptions.Item label="实例 ID">{host.id}</Descriptions.Item>
            <Descriptions.Item label="Agent ID">{host.agent_id || '-'}</Descriptions.Item>
            <Descriptions.Item label="区域">{host.region}</Descriptions.Item>
            <Descriptions.Item label="地址">{host.address}</Descriptions.Item>
            <Descriptions.Item label="状态"><StatusBadge status={host.status} /></Descriptions.Item>
            <Descriptions.Item label="最近指标">{formatDateTime(host.last_metric_at)}</Descriptions.Item>
          </Descriptions>
        ) : null}
      </section>

      <section className="control-table-frame">
        <Table<RuntimeSummary>
          columns={columns}
          dataSource={runtimes}
          loading={loading}
          rowKey="id"
          pagination={false}
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description="该实例下暂无 Caster 节点" /> }}
        />
      </section>

      <Modal title="新增 Caster 节点" open={createOpen} onCancel={() => setCreateOpen(false)} onOk={() => createForm.submit()} destroyOnHidden>
        <Form form={createForm} layout="vertical" requiredMark={false} onFinish={submitCreate}>
          <Form.Item name="name" label="节点名称" rules={[{ required: true, message: '请输入节点名称' }]}>
            <Input />
          </Form.Item>
          <Form.Item name="listen_port" label="监听端口" rules={[{ required: true, message: '请输入监听端口' }]}>
            <InputNumber min={1} max={65535} className="identity-number-input" />
          </Form.Item>
          <div className="node-form-grid">
            <Form.Item name="worker_count" label="工作线程" rules={[{ required: true, message: '请输入工作线程数' }]}>
              <InputNumber min={1} className="identity-number-input" />
            </Form.Item>
            <Form.Item name="max_worker_count" label="最大线程" rules={[{ required: true, message: '请输入最大线程数' }]}>
              <InputNumber min={1} className="identity-number-input" />
            </Form.Item>
          </div>
          <div className="node-form-grid">
            <Form.Item name="config_version" label="配置版本" rules={[{ required: true, message: '请输入配置版本' }]}>
              <InputNumber min={1} className="identity-number-input" />
            </Form.Item>
            <Form.Item name="desired_state" label="期望状态">
              <Select options={[
                { label: '运行中', value: 'running' },
                { label: '排空中', value: 'draining' },
                { label: '已停止', value: 'stopped' },
              ]} />
            </Form.Item>
          </div>
          <Form.Item name="restart_policy" label="重启策略">
            <Select<RuntimeRestartPolicy> options={[
              { label: restartPolicyLabel('always'), value: 'always' },
              { label: restartPolicyLabel('on_failure'), value: 'on_failure' },
              { label: restartPolicyLabel('never'), value: 'never' },
            ]} />
          </Form.Item>
          <Form.Item name="start_immediately" label="创建后立即启动">
            <Select options={[
              { label: '立即启动', value: true },
              { label: '仅保存期望配置', value: false },
            ]} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title={`配置 Caster：${editingRuntime?.name ?? ''}`} open={Boolean(editingRuntime)} onCancel={() => setEditingRuntime(null)} onOk={() => configForm.submit()} destroyOnHidden>
        <Form form={configForm} layout="vertical" requiredMark={false} onFinish={submitConfig}>
          <Form.Item name="desired_state" label="期望状态">
            <Select options={[
              { label: '运行中', value: 'running' },
              { label: '排空中', value: 'draining' },
              { label: '已停止', value: 'stopped' },
            ]} />
          </Form.Item>
          <div className="node-form-grid">
            <Form.Item name="listen_port" label="监听端口">
              <InputNumber min={1} max={65535} className="identity-number-input" />
            </Form.Item>
            <Form.Item name="config_version" label="配置版本">
              <InputNumber min={1} className="identity-number-input" />
            </Form.Item>
          </div>
          <div className="node-form-grid">
            <Form.Item name="worker_count" label="工作线程">
              <InputNumber min={1} className="identity-number-input" />
            </Form.Item>
            <Form.Item name="max_worker_count" label="最大线程">
              <InputNumber min={1} className="identity-number-input" />
            </Form.Item>
          </div>
          <Form.Item name="restart_policy" label="重启策略">
            <Select<RuntimeRestartPolicy> options={[
              { label: restartPolicyLabel('always'), value: 'always' },
              { label: restartPolicyLabel('on_failure'), value: 'on_failure' },
              { label: restartPolicyLabel('never'), value: 'never' },
            ]} />
          </Form.Item>
          <Form.Item name="draining" label="排空模式">
            <Select options={[
              { label: '开启排空', value: true },
              { label: '关闭排空', value: false },
            ]} />
          </Form.Item>
        </Form>
      </Modal>

      <ConfirmDialog
        open={Boolean(targetAction)}
        title="提交 Caster 节点操作"
        description={targetAction ? `为 ${targetAction.runtime.name} 提交${runtimeActionLabel(targetAction.action)}意图。` : ''}
        intentLabel={targetAction ? `实例 ${host?.name ?? id} 下的节点${runtimeActionLabel(targetAction.action)}` : ''}
        confirmText="提交意图"
        onCancel={() => setTargetAction(null)}
        onConfirm={submitAction}
      />
    </div>
  );
}
