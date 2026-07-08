import { Link, useNavigate, useParams } from 'react-router-dom';
import { ArrowLeftOutlined, ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Descriptions, Empty, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { adminService } from '../../api/adminService';
import { apiErrorMessage, getAdminAccessAccount, getMyAccessAccount, listAdminAccessAccounts, type AccessAccount } from '../../api/identity';
import type { RuntimeSummary } from '../../api/contracts';
import { AccountStatusTag } from './IdentityBadges';
import { formatIdentityTime } from './identityFormat';
import { buildMobileStations, humanBps, humanDuration, type MobileStationRow } from './accessMonitorModel';
import { formatDateTime } from './useControlPage';

type LoginHistoryRow = {
  id: string;
  startedAt: string;
  endedAt: string;
  clientIp: string;
  mountPoint: string;
  duration: string;
  traffic: string;
  result: string;
};

async function getAdminAccessAccountCompat(accessAccountId: string) {
  try {
    return await getAdminAccessAccount(accessAccountId);
  } catch (firstError) {
    for (let page = 1; page <= 20; page += 1) {
      const result = await listAdminAccessAccounts({ page, pageSize: 200, status: 'all' });
      const found = result.items.find((item) => item.access_account_id === accessAccountId);
      if (found) return found;
      if (page * result.pageSize >= result.total) break;
    }
    throw firstError;
  }
}

export default function AccessAccountDetailPage({ scope = 'admin' }: { scope?: 'admin' | 'user' }) {
  const { id = '' } = useParams();
  const navigate = useNavigate();
  const [account, setAccount] = useState<AccessAccount | null>(null);
  const [runtimes, setRuntimes] = useState<RuntimeSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const refresh = useCallback(async () => {
    if (!id) return;
    setLoading(true);
    try {
      const [accountResult, runtimeResult] = await Promise.all([
        scope === 'admin' ? getAdminAccessAccountCompat(id) : getMyAccessAccount(id),
        adminService.listRuntimes({ page: 1, pageSize: 500, status: 'all' }),
      ]);
      setAccount(accountResult);
      setRuntimes(runtimeResult.items);
      setError('');
    } catch (err) {
      setAccount(null);
      setRuntimes([]);
      setError(apiErrorMessage(err));
    } finally {
      setLoading(false);
    }
  }, [id, scope]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const currentSessions = useMemo(() => {
    if (!account) return [];
    return buildMobileStations(runtimes).filter((row) => row.accessAccount === account.username);
  }, [account, runtimes]);

  const historyRows: LoginHistoryRow[] = useMemo(() => {
    return currentSessions.map((row, index) => ({
      id: `${row.id}-history`,
      startedAt: row.connectedAt,
      endedAt: '在线中',
      clientIp: row.clientIp,
      mountPoint: row.mountPoint,
      duration: humanDuration(row.durationSeconds),
      traffic: `${row.trafficMb.toFixed(2)} MB`,
      result: index === 0 ? '当前连接' : '并发连接',
    }));
  }, [currentSessions]);

  const sessionColumns: ColumnsType<MobileStationRow> = [
    { title: '挂载点', dataIndex: 'mountPoint', fixed: 'left', width: 180 },
    { title: '节点', dataIndex: 'runtimeName', render: (_, row) => <Link to={`/admin/control/runtimes/${row.runtimeId}`}>{row.runtimeName}</Link> },
    { title: '客户端 IP', dataIndex: 'clientIp' },
    { title: '时长', dataIndex: 'durationSeconds', render: humanDuration },
    { title: '流量', dataIndex: 'trafficMb', render: (value) => `${value.toFixed(2)} MB` },
    { title: '下行', dataIndex: 'sendBps', render: humanBps },
    { title: '上行', dataIndex: 'recvBps', render: humanBps },
    { title: '最近活动', dataIndex: 'lastSeenAt', render: formatDateTime },
  ];

  const historyColumns: ColumnsType<LoginHistoryRow> = [
    { title: '登录时间', dataIndex: 'startedAt', fixed: 'left', width: 190, render: formatDateTime },
    { title: '下线时间', dataIndex: 'endedAt', render: (value) => (value === '在线中' ? value : formatDateTime(value)) },
    { title: '客户端 IP', dataIndex: 'clientIp' },
    { title: '挂载点', dataIndex: 'mountPoint' },
    { title: '时长', dataIndex: 'duration' },
    { title: '流量', dataIndex: 'traffic' },
    { title: '结果', dataIndex: 'result' },
  ];

  return (
    <div className="control-detail-page access-account-detail-page">
      <section className="control-detail-toolbar control-table-toolbar">
        <Button
          className="node-back-button"
          type="text"
          icon={<ArrowLeftOutlined />}
          onClick={() => navigate(scope === 'admin' ? '/admin/control/access' : '/admin/control/user/access-accounts')}
        >
          {scope === 'admin' ? '返回账号管理' : '返回我的账号'}
        </Button>
        <Space className="control-table-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} loading={loading} onClick={refresh}>刷新</Button>
        </Space>
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="接入账号数据不可用" description={error} /> : null}

      <section className="control-panel">
        <h2>账号状态</h2>
        {account ? (
          <Descriptions size="small" column={2}>
            <Descriptions.Item label="接入账号 ID">{account.access_account_id}</Descriptions.Item>
            <Descriptions.Item label="状态"><AccountStatusTag status={account.status} /></Descriptions.Item>
            <Descriptions.Item label="显示名">{account.display_name || '-'}</Descriptions.Item>
            <Descriptions.Item label="归属用户">{account.owner_username || account.owner_account_id}</Descriptions.Item>
            <Descriptions.Item label="备注">{account.note || '-'}</Descriptions.Item>
            <Descriptions.Item label="过期时间">{formatIdentityTime(account.expires_at)}</Descriptions.Item>
            <Descriptions.Item label="创建时间">{formatIdentityTime(account.created_at)}</Descriptions.Item>
            <Descriptions.Item label="更新时间">{formatIdentityTime(account.updated_at)}</Descriptions.Item>
          </Descriptions>
        ) : (
          <Empty description={loading ? '正在加载接入账号' : '没有找到该接入账号'} />
        )}
      </section>

      <section className="control-panel">
        <h2>本次在线使用状态</h2>
        <Table<MobileStationRow>
          columns={sessionColumns}
          dataSource={currentSessions}
          rowKey="id"
          loading={loading}
          pagination={false}
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description="当前没有匹配到在线移动站连接" /> }}
        />
      </section>

      <section className="control-panel">
        <h2>历史登录记录</h2>
        <Table<LoginHistoryRow>
          columns={historyColumns}
          dataSource={historyRows}
          rowKey="id"
          loading={loading}
          pagination={false}
          scroll={{ x: 'max-content' }}
          locale={{ emptyText: <Empty description="后端接入账号登录历史接口补齐后将在这里展示真实记录" /> }}
        />
      </section>
    </div>
  );
}
