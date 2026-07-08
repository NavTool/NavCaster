import { Link, useNavigate, useParams } from 'react-router-dom';
import { ArrowLeftOutlined, ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Descriptions, Empty, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { adminService } from '../../api/adminService';
import type { RuntimeSummary } from '../../api/contracts';
import { StatusBadge } from '../components/StatusBadge';
import { formatDateTime } from './useControlPage';
import {
  buildBaseStationHistory,
  buildMobileUsageHistory,
  findBaseStation,
  findMobileStation,
  type BaseStationHistoryRow,
  type MobileUsageHistoryRow,
} from './accessMonitorModel';

export default function AccessMonitorDetailPage({ mode }: { mode: 'base' | 'mobile' }) {
  const { id = '' } = useParams();
  const navigate = useNavigate();
  const [runtimes, setRuntimes] = useState<RuntimeSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await adminService.listRuntimes({ page: 1, pageSize: 500, status: 'all' });
      setRuntimes(result.items);
      setError('');
    } catch (err) {
      setRuntimes([]);
      setError(err instanceof Error ? err.message : '详情数据加载失败');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const base = useMemo(() => (mode === 'base' ? findBaseStation(runtimes, id) : undefined), [id, mode, runtimes]);
  const mobile = useMemo(() => (mode === 'mobile' ? findMobileStation(runtimes, id) : undefined), [id, mode, runtimes]);
  const baseHistoryColumns: ColumnsType<BaseStationHistoryRow> = [
    { title: '上线时间', dataIndex: 'startedAt', fixed: 'left', width: 190, render: formatDateTime },
    { title: '下线时间', dataIndex: 'endedAt', render: (value) => (value === '在线中' ? value : formatDateTime(value)) },
    { title: '在线时长', dataIndex: 'duration' },
    { title: '峰值客户端', dataIndex: 'peakClients', align: 'right' },
    { title: '流量', dataIndex: 'traffic' },
    { title: '结束原因', dataIndex: 'endReason' },
  ];

  const mobileHistoryColumns: ColumnsType<MobileUsageHistoryRow> = [
    { title: '开始时间', dataIndex: 'startedAt', fixed: 'left', width: 190, render: formatDateTime },
    { title: '结束时间', dataIndex: 'endedAt', render: (value) => (value === '在线中' ? value : formatDateTime(value)) },
    { title: '挂载点', dataIndex: 'mountPoint' },
    { title: '流量', dataIndex: 'traffic' },
    { title: '时长', dataIndex: 'duration' },
    { title: '费用', dataIndex: 'cost' },
    { title: '断开次数', dataIndex: 'disconnects', align: 'right' },
    { title: '结束原因', dataIndex: 'endReason' },
  ];

  const historyRows = mode === 'base' && base ? buildBaseStationHistory(base) : mode === 'mobile' && mobile ? buildMobileUsageHistory(mobile) : [];

  return (
    <div className="control-detail-page monitor-detail-page">
      <section className="control-detail-toolbar control-table-toolbar">
        <Button
          className="node-back-button"
          type="text"
          icon={<ArrowLeftOutlined />}
          onClick={() => navigate(mode === 'base' ? '/admin/control/access-monitor/base-stations' : '/admin/control/access-monitor/mobile-stations')}
        >
          返回接入监控
        </Button>
        <Space className="control-table-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
        </Space>
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="详情数据不可用" description={error} /> : null}

      {mode === 'base' && base ? (
        <>
          <section className="control-panel">
            <h2>基准站信息</h2>
            <Descriptions size="small" column={2}>
              <Descriptions.Item label="状态"><StatusBadge status={base.status} /></Descriptions.Item>
              <Descriptions.Item label="挂载点">{base.mountPoint}</Descriptions.Item>
              <Descriptions.Item label="分组">{base.groupName}</Descriptions.Item>
              <Descriptions.Item label="源站账号">{base.sourceAccount}</Descriptions.Item>
              <Descriptions.Item label="所在节点"><Link to={`/admin/control/runtimes/${base.runtimeId}`}>{base.runtimeName}</Link></Descriptions.Item>
              <Descriptions.Item label="所在实例">{base.hostName}</Descriptions.Item>
              <Descriptions.Item label="源站地址">{base.address}</Descriptions.Item>
              <Descriptions.Item label="最近上报">{formatDateTime(base.lastSeenAt)}</Descriptions.Item>
            </Descriptions>
          </section>
        </>
      ) : null}

      {mode === 'mobile' && mobile ? (
        <>
          <section className="control-panel">
            <h2>移动站连接</h2>
            <Descriptions size="small" column={2}>
              <Descriptions.Item label="状态"><StatusBadge status={mobile.status} /></Descriptions.Item>
              <Descriptions.Item label="接入账号">{mobile.accessAccount}</Descriptions.Item>
              <Descriptions.Item label="显示名">{mobile.displayName}</Descriptions.Item>
              <Descriptions.Item label="归属账号">{mobile.ownerAccount}</Descriptions.Item>
              <Descriptions.Item label="挂载点">{mobile.mountPoint}</Descriptions.Item>
              <Descriptions.Item label="客户端 IP">{mobile.clientIp}</Descriptions.Item>
              <Descriptions.Item label="所在节点"><Link to={`/admin/control/runtimes/${mobile.runtimeId}`}>{mobile.runtimeName}</Link></Descriptions.Item>
              <Descriptions.Item label="最近活动">{formatDateTime(mobile.lastSeenAt)}</Descriptions.Item>
            </Descriptions>
          </section>
        </>
      ) : null}

      {!base && !mobile && !loading ? (
        <section className="control-panel">
          <Empty description="当前快照中没有找到该接入对象，可能已经离线或历史记录接口尚未接入" />
        </section>
      ) : null}

      <section className="control-panel">
        <h2>{mode === 'base' ? '历史在线记录' : '历史使用记录'}</h2>
        <Table<BaseStationHistoryRow | MobileUsageHistoryRow>
          columns={(mode === 'base' ? baseHistoryColumns : mobileHistoryColumns) as ColumnsType<BaseStationHistoryRow | MobileUsageHistoryRow>}
          dataSource={historyRows}
          rowKey="id"
          loading={loading}
          pagination={false}
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description="后端历史明细接口接入后将在这里展示真实记录" /> }}
        />
      </section>
    </div>
  );
}
