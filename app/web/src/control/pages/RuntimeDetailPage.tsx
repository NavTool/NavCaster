import { useParams } from 'react-router-dom';
import { PauseCircleOutlined, PlayCircleOutlined, ReloadOutlined, WarningOutlined } from '@ant-design/icons';
import { Alert, Button, Descriptions, Empty, Space, Spin, Table, Timeline, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { RuntimeActionIntent, RuntimeDetail, WorkerMetric } from '../../api/contracts';
import { ConfirmDialog } from '../components/ConfirmDialog';
import { ConvergenceBadge, StatusBadge } from '../components/StatusBadge';
import { desiredRuntimeStateLabel, runtimeActionLabel, runtimeKindLabel } from '../labels';
import { formatDateTime } from './useControlPage';

type RuntimeIntentAction = RuntimeActionIntent['action'];

export default function RuntimeDetailPage() {
  const { id = '' } = useParams();
  const [runtime, setRuntime] = useState<RuntimeDetail | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [action, setAction] = useState<RuntimeIntentAction | null>(null);
  const [lastIntent, setLastIntent] = useState<{ action: RuntimeIntentAction; message: string } | null>(null);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      setRuntime(await adminService.getRuntime(id));
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : '加载运行时详情失败');
      setRuntime(null);
    } finally {
      setLoading(false);
    }
  }, [id]);

  useEffect(() => { void load(); }, [load]);

  async function submitAction(reason: string) {
    if (!runtime || !action) return;
    const receipt = await adminService.submitRuntimeAction({ runtime_id: runtime.id, action, reason });
    message.success(receipt.message);
    setLastIntent({ action, message: receipt.message });
    setAction(null);
    await load();
  }

  if (loading) return <Spin size="large" className="control-center-spin" />;
  if (error) return <Alert type="error" showIcon message="运行时详情不可用" description={error} />;
  if (!runtime) return <Empty description="没有找到运行时" />;

  const workerColumns: ColumnsType<WorkerMetric> = [
    { title: '工作线程', dataIndex: 'name', fixed: 'left', width: 200 },
    { title: '状态', dataIndex: 'status', render: (status) => <StatusBadge status={status} /> },
    { title: '挂载点', dataIndex: 'assigned_mount_points', align: 'right' },
    { title: '会话', dataIndex: 'active_sessions', align: 'right' },
    { title: '吞吐', dataIndex: 'throughput_kbps', render: (value) => `${value.toLocaleString()} kbps` },
    { title: 'P95 延迟', dataIndex: 'latency_p95_ms', render: (value) => `${value} ms` },
    { title: '错误率', dataIndex: 'error_rate', render: (value) => `${value}%` },
  ];

  return (
    <div className="control-detail-page">
      <section className="control-detail-toolbar control-table-toolbar">
        <Space className="control-table-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} onClick={load}>刷新</Button>
          <Button icon={<PlayCircleOutlined />} onClick={() => setAction('start')}>启动意图</Button>
          <Button icon={<PauseCircleOutlined />} onClick={() => setAction('stop')}>停止意图</Button>
          <Button icon={<ReloadOutlined />} onClick={() => setAction('restart')}>重启意图</Button>
          <Button danger icon={<WarningOutlined />} onClick={() => setAction('drain')}>排空意图</Button>
        </Space>
      </section>

      {lastIntent ? (
        <Alert
          className="control-intent-alert"
          showIcon
          type={runtime.convergence_status === 'converged' ? 'success' : 'warning'}
          message={runtime.convergence_status === 'converged' ? '意图已观测为收敛' : '意图已接受，等待实际状态上报'}
          description={`${runtimeActionLabel(lastIntent.action)}意图。${runtime.convergence_detail || lastIntent.message}`}
        />
      ) : null}

      <section className="control-detail-grid">
        <div className="control-panel">
          <h2>期望与实际</h2>
          <Descriptions column={1} size="small">
            <Descriptions.Item label="主机">{runtime.host_name}</Descriptions.Item>
            <Descriptions.Item label="类型">{runtimeKindLabel(runtime.kind)}</Descriptions.Item>
            <Descriptions.Item label="期望状态">{desiredRuntimeStateLabel(runtime.desired_state)}</Descriptions.Item>
            <Descriptions.Item label="实际状态"><StatusBadge status={runtime.status} /></Descriptions.Item>
            <Descriptions.Item label="收敛状态"><ConvergenceBadge status={runtime.convergence_status} /></Descriptions.Item>
            <Descriptions.Item label="期望版本">{runtime.desired_version || '-'}</Descriptions.Item>
            <Descriptions.Item label="已观测版本">{runtime.observed_desired_version || '-'}</Descriptions.Item>
            <Descriptions.Item label="期望配置">{runtime.target_config_version_id}</Descriptions.Item>
            <Descriptions.Item label="实际配置">{runtime.current_config_version_id}</Descriptions.Item>
            <Descriptions.Item label="期望工作线程">{runtime.desired_worker_count || '-'}</Descriptions.Item>
            <Descriptions.Item label="实际工作线程">{runtime.actual_worker_count || '-'}</Descriptions.Item>
            <Descriptions.Item label="监听端口">{runtime.listen_port || '-'}</Descriptions.Item>
            <Descriptions.Item label="进程 ID">{runtime.process_id || '-'}</Descriptions.Item>
            <Descriptions.Item label="Redis 连接">{runtime.redis_connected === undefined ? '-' : runtime.redis_connected ? '是' : '否'}</Descriptions.Item>
            <Descriptions.Item label="连接数">{runtime.active_sessions}</Descriptions.Item>
            <Descriptions.Item label="挂载点 / 源站 / 客户端">{`${runtime.mounts} / ${runtime.sources} / ${runtime.clients}`}</Descriptions.Item>
            <Descriptions.Item label="Loop p95">{runtime.loop_delay_ms_p95 ? `${runtime.loop_delay_ms_p95} ms` : '-'}</Descriptions.Item>
            <Descriptions.Item label="下行 / 上行 bps">{`${runtime.send_bps.toLocaleString()} / ${runtime.recv_bps.toLocaleString()}`}</Descriptions.Item>
            <Descriptions.Item label="命令">{runtime.command}</Descriptions.Item>
            <Descriptions.Item label="环境配置">{runtime.env_profile}</Descriptions.Item>
            <Descriptions.Item label="期望更新时间">{formatDateTime(runtime.desired_updated_at)}</Descriptions.Item>
            <Descriptions.Item label="实际更新时间">{formatDateTime(runtime.actual_updated_at)}</Descriptions.Item>
            <Descriptions.Item label="最近指标">{formatDateTime(runtime.last_metric_at)}</Descriptions.Item>
            <Descriptions.Item label="指标新鲜度">{runtime.stale_detail}</Descriptions.Item>
            <Descriptions.Item label="最近错误">{runtime.last_error || '-'}</Descriptions.Item>
          </Descriptions>
          <p className="control-panel-note">{runtime.desired_state_note}</p>
        </div>

        <div className="control-panel">
          <h2>事件</h2>
          {runtime.recent_events.length > 0 ? (
            <Timeline
              items={runtime.recent_events.map((event) => ({
                color: event.level === 'error' ? 'red' : event.level === 'warning' ? 'orange' : 'green',
                children: (
                  <div className="control-event">
                    <strong>{event.message}</strong>
                    <span>{[event.type, event.desired_version ? `期望版本 ${event.desired_version}` : '', event.process_id ? `进程 ${event.process_id}` : ''].filter(Boolean).join(' / ') || '运行时事件'}</span>
                    <span>{formatDateTime(event.created_at)}</span>
                  </div>
                ),
              }))}
            />
          ) : (
            <Empty description="AdminService 没有返回运行时事件" />
          )}
        </div>
      </section>

      <section className="control-panel">
        <h2>工作线程</h2>
        <div className="control-table-frame">
          <Table<WorkerMetric>
            columns={workerColumns}
            dataSource={runtime.workers}
            pagination={false}
            rowKey="id"
            size="middle"
            scroll={{ x: 'max-content' }}
            locale={{ emptyText: <Empty description="AdminService 没有返回工作线程数据" /> }}
          />
        </div>
      </section>

      <ConfirmDialog
        open={Boolean(action)}
        title="提交运行时操作意图"
        description={action ? `为 ${runtime.name} 加入${runtimeActionLabel(action)}意图。` : ''}
        intentLabel={action ? `通过 AdminService 请求${runtimeActionLabel(action)}` : ''}
        confirmText="加入操作意图队列"
        onCancel={() => setAction(null)}
        onConfirm={submitAction}
      />
    </div>
  );
}
