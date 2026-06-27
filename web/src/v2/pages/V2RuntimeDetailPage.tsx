import { useParams } from 'react-router-dom';
import { BranchesOutlined, PlayCircleOutlined, ReloadOutlined, WarningOutlined } from '@ant-design/icons';
import { Button, Descriptions, Empty, Space, Spin, Table, Timeline, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { v2AdminService } from '../api/adminService';
import type { RuntimeActionIntent, RuntimeDetail, WorkerMetric } from '../api/contracts';
import { V2ConfirmDialog } from '../components/V2ConfirmDialog';
import { V2MetricCard } from '../components/V2MetricCard';
import { V2ConvergenceBadge, V2StatusBadge } from '../components/V2StatusBadge';
import { formatDateTime } from './useV2Page';

type RuntimeIntentAction = RuntimeActionIntent['action'];

export default function V2RuntimeDetailPage() {
  const { id = '' } = useParams();
  const [runtime, setRuntime] = useState<RuntimeDetail | null>(null);
  const [loading, setLoading] = useState(true);
  const [action, setAction] = useState<RuntimeIntentAction | null>(null);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      setRuntime(await v2AdminService.getRuntime(id));
    } finally {
      setLoading(false);
    }
  }, [id]);

  useEffect(() => { void load(); }, [load]);

  async function submitAction(reason: string) {
    if (!runtime || !action) return;
    const receipt = await v2AdminService.submitRuntimeAction({ runtime_id: runtime.id, action, reason, target_config_version_id: runtime.target_config_version_id });
    message.success(receipt.message);
    setAction(null);
    await load();
  }

  if (loading) return <Spin size="large" className="v2-center-spin" />;
  if (!runtime) return <Empty description="Runtime not found" />;

  const workerColumns: ColumnsType<WorkerMetric> = [
    { title: 'Worker', dataIndex: 'name' },
    { title: 'State', dataIndex: 'status', render: (status) => <V2StatusBadge status={status} /> },
    { title: 'Mount points', dataIndex: 'assigned_mount_points', align: 'right' },
    { title: 'Sessions', dataIndex: 'active_sessions', align: 'right' },
    { title: 'Throughput', dataIndex: 'throughput_kbps', render: (value) => `${value.toLocaleString()} kbps` },
    { title: 'P95 latency', dataIndex: 'latency_p95_ms', render: (value) => `${value} ms` },
    { title: 'Error rate', dataIndex: 'error_rate', render: (value) => `${value}%` },
  ];

  return (
    <div className="v2-detail-page">
      <div className="v2-page-heading">
        <div>
          <h1>{runtime.name}</h1>
          <p>{runtime.id}</p>
        </div>
        <Space wrap>
          <Button icon={<ReloadOutlined />} onClick={load}>Refresh</Button>
          <Button icon={<PlayCircleOutlined />} onClick={() => setAction('restart')}>Restart intent</Button>
          <Button icon={<BranchesOutlined />} type="primary" onClick={() => setAction('roll-config')}>Roll config intent</Button>
          <Button danger icon={<WarningOutlined />} onClick={() => setAction('drain-workers')}>Drain workers intent</Button>
        </Space>
      </div>

      <div className="v2-metric-grid">
        <V2MetricCard label="Convergence" value={<V2ConvergenceBadge status={runtime.convergence_status} />} detail={runtime.convergence_detail} />
        <V2MetricCard label="Actual state" value={<V2StatusBadge status={runtime.status} />} detail={`desired: ${runtime.desired_state}`} />
        <V2MetricCard label="Workers" value={`${runtime.actual_worker_count} / ${runtime.desired_worker_count}`} detail="actual / desired" />
        <V2MetricCard label="Loop p95" value={runtime.loop_delay_ms_p95 ? `${runtime.loop_delay_ms_p95} ms` : '-'} detail="last metric snapshot" />
      </div>

      <section className="v2-detail-grid">
        <div className="v2-panel">
          <h2>Desired vs actual</h2>
          <Descriptions column={1} size="small">
            <Descriptions.Item label="Host">{runtime.host_name}</Descriptions.Item>
            <Descriptions.Item label="Kind">{runtime.kind}</Descriptions.Item>
            <Descriptions.Item label="Desired state">{runtime.desired_state}</Descriptions.Item>
            <Descriptions.Item label="Actual state"><V2StatusBadge status={runtime.status} /></Descriptions.Item>
            <Descriptions.Item label="Convergence"><V2ConvergenceBadge status={runtime.convergence_status} /></Descriptions.Item>
            <Descriptions.Item label="Desired version">{runtime.desired_version || '-'}</Descriptions.Item>
            <Descriptions.Item label="Observed version">{runtime.observed_desired_version || '-'}</Descriptions.Item>
            <Descriptions.Item label="Desired config">{runtime.target_config_version_id}</Descriptions.Item>
            <Descriptions.Item label="Actual config">{runtime.current_config_version_id}</Descriptions.Item>
            <Descriptions.Item label="Desired workers">{runtime.desired_worker_count || '-'}</Descriptions.Item>
            <Descriptions.Item label="Actual workers">{runtime.actual_worker_count || '-'}</Descriptions.Item>
            <Descriptions.Item label="Listen port">{runtime.listen_port || '-'}</Descriptions.Item>
            <Descriptions.Item label="Process ID">{runtime.process_id || '-'}</Descriptions.Item>
            <Descriptions.Item label="Redis connected">{runtime.redis_connected === undefined ? '-' : runtime.redis_connected ? 'yes' : 'no'}</Descriptions.Item>
            <Descriptions.Item label="Send / recv bps">{`${runtime.send_bps.toLocaleString()} / ${runtime.recv_bps.toLocaleString()}`}</Descriptions.Item>
            <Descriptions.Item label="Image">{runtime.image}</Descriptions.Item>
            <Descriptions.Item label="Command">{runtime.command}</Descriptions.Item>
            <Descriptions.Item label="Desired updated">{formatDateTime(runtime.desired_updated_at)}</Descriptions.Item>
            <Descriptions.Item label="Last metric">{formatDateTime(runtime.last_metric_at)}</Descriptions.Item>
            <Descriptions.Item label="Last error">{runtime.last_error || '-'}</Descriptions.Item>
          </Descriptions>
          <p className="v2-panel-note">{runtime.desired_state_note}</p>
        </div>

        <div className="v2-panel">
          <h2>Events</h2>
          <Timeline
            items={runtime.recent_events.map((event) => ({
              color: event.level === 'error' ? 'red' : event.level === 'warning' ? 'orange' : 'green',
              children: <div className="v2-event"><strong>{event.message}</strong><span>{formatDateTime(event.created_at)}</span></div>,
            }))}
          />
        </div>
      </section>

      <section className="v2-panel">
        <h2>Workers</h2>
        <div className="v2-table-frame">
          <Table<WorkerMetric> columns={workerColumns} dataSource={runtime.workers} pagination={false} rowKey="id" size="middle" />
        </div>
      </section>

      <V2ConfirmDialog
        open={Boolean(action)}
        title="Submit runtime action intent"
        description={action ? `Queue ${action} for ${runtime.name}.` : ''}
        intentLabel={action ? `Request ${action} through AdminService` : ''}
        confirmText="Queue action intent"
        onCancel={() => setAction(null)}
        onConfirm={submitAction}
      />
    </div>
  );
}
