import React, { useCallback } from 'react';
import { Typography, Card, Breadcrumb, Empty, Descriptions, Table, Tag } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { usePolling } from '../hooks/usePolling';
import { accountsApi, accountActivesApi, getUsrHistory } from '../api';
import type { AccountRecord, AccountActive } from '../api/types';
import { AccountType, AccountStateType } from '../api/types';
import { getLocalTime } from '../utils/format';
import ConnectionHistoryTable from '../components/ConnectionHistoryTable';

const { Title } = Typography;

const typeLabels: Record<number, string> = {
  [AccountType.ACCOUNT_TYPE_LONG_TERM]: '长期',
  [AccountType.ACCOUNT_TYPE_EXPIRE_BY_DATE]: '按日期',
  [AccountType.ACCOUNT_TYPE_EXPIRE_BY_USAGE]: '按用量',
};

const stateLabels: Record<number, { text: string; color: string }> = {
  [AccountStateType.ACCOUNT_STATE_TYPE_NORMAL]: { text: '正常', color: 'green' },
  [AccountStateType.ACCOUNT_STATE_TYPE_FROZEN]: { text: '冻结', color: 'orange' },
  [AccountStateType.ACCOUNT_STATE_TYPE_EXPIRED]: { text: '过期', color: 'red' },
};

const AccountDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: accounts } = usePolling(() => accountsApi.getAll(), 5000);
  const { data: actives } = usePolling(() => accountActivesApi.getAll(), 3000);
  const account: AccountRecord | undefined = accounts?.[id || ''];
  const accountName = account?.account || '';
  const fetchHistory = useCallback(() => getUsrHistory(accountName), [accountName]);

  const activeList = actives
    ? Object.values(actives).filter(a => a.account === account?.account)
    : [];

  const activeColumns = [
    { title: 'IP', dataIndex: 'addr', key: 'addr' },
    { title: '端口', dataIndex: 'port', key: 'port' },
    { title: '上线时间', key: 'online_time', render: (_: unknown, r: AccountActive) => getLocalTime(r.online_time) },
  ];

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/accounts">账号管理</Link> },
        { title: account?.account || id || '账号详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{account?.account || id}</Title>

      {account ? (
        <>
          <Card title="账号信息" style={{ marginBottom: 16, borderColor: '#2e3450' }}>
            <Descriptions column={2} size="small">
              <Descriptions.Item label="账号">{account.account}</Descriptions.Item>
              <Descriptions.Item label="类型">{typeLabels[account.type] || '未知'}</Descriptions.Item>
              <Descriptions.Item label="状态">
                <Tag color={stateLabels[account.state]?.color || 'default'}>
                  {stateLabels[account.state]?.text || '未知'}
                </Tag>
              </Descriptions.Item>
              <Descriptions.Item label="并发限制">{account.connection_limit}</Descriptions.Item>
              <Descriptions.Item label="联系人">{account.contact_person || '-'}</Descriptions.Item>
              <Descriptions.Item label="联系方式">{account.contact_info || '-'}</Descriptions.Item>
              <Descriptions.Item label="创建时间">{getLocalTime(account.create_time)}</Descriptions.Item>
              <Descriptions.Item label="备注">{account.remark || '-'}</Descriptions.Item>
            </Descriptions>
          </Card>

          <Card title={`当前在线 (${activeList.length})`} style={{ marginBottom: 16, borderColor: '#2e3450' }}>
            <Table
              columns={activeColumns}
              dataSource={activeList.map(a => ({ ...a, key: a.uid }))}
              size="small"
              pagination={false}
            />
          </Card>

          <Card title="登录记录" style={{ borderColor: '#2e3450' }}>
            {accountName ? <ConnectionHistoryTable fetchData={fetchHistory} showMount /> : <Empty description="暂无数据" />}
          </Card>
        </>
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到账号 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default AccountDetail;
