import { ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Input, Select, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { apiErrorMessage, listAdminAccessAccounts, type AccessAccount, type AccessAccountStatus } from '../../api/identity';
import { AccountStatusTag } from './IdentityBadges';
import { formatIdentityTime } from './identityFormat';

const statuses: Array<AccessAccountStatus | 'all'> = ['all', 'active', 'disabled', 'deleted'];

export default function AdminAccessAccountsPage() {
  const [rows, setRows] = useState<AccessAccount[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [query, setQuery] = useState({ page: 1, pageSize: 10, search: '', status: 'all' as AccessAccountStatus | 'all' });

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await listAdminAccessAccounts(query);
      setRows(result.items);
      setTotal(result.total);
      setError('');
    } catch (err) {
      setRows([]);
      setTotal(0);
      setError(apiErrorMessage(err));
    } finally {
      setLoading(false);
    }
  }, [query]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const columns: ColumnsType<AccessAccount> = [
    {
      title: '接入账号',
      dataIndex: 'username',
      fixed: 'left',
      render: (_, row) => (
        <div className="control-primary-cell">
          <strong>{row.username}</strong>
          <span>{row.access_account_id}</span>
        </div>
      ),
    },
    { title: '归属用户', dataIndex: 'owner_username', render: (_, row) => row.owner_username || row.owner_account_id },
    { title: '显示名', dataIndex: 'display_name', render: (value) => value || '-' },
    { title: '状态', dataIndex: 'status', render: (status) => <AccountStatusTag status={status} /> },
    { title: '并发限制', dataIndex: 'concurrency_limit', align: 'right', render: (value) => (value === 0 ? '不限' : value) },
    { title: '投影版本', dataIndex: 'projection_version', align: 'right', render: (value) => value ?? '-' },
    { title: '过期时间', dataIndex: 'expires_at', render: formatIdentityTime },
    { title: '更新时间', dataIndex: 'updated_at', render: formatIdentityTime },
  ];

  return (
    <div className="identity-page">
      <div className="control-page-heading">
        <div>
          <h1>接入账号查询</h1>
          <p>管理员全局只读查看设备、客户端或基站使用的接入账号；创建和状态变更由账号归属用户完成。</p>
        </div>
        <Space wrap>
          <Button icon={<ReloadOutlined />} onClick={refresh}>刷新</Button>
        </Space>
      </div>

      <section className="identity-toolbar">
        <Input.Search
          allowClear
          placeholder="搜索接入账号或显示名"
          value={query.search}
          onChange={(event) => setQuery((prev) => ({ ...prev, search: event.target.value, page: 1 }))}
        />
        <Select
          value={query.status}
          options={statuses.map((status) => ({ label: status === 'all' ? '全部状态' : status, value: status }))}
          onChange={(status) => setQuery((prev) => ({ ...prev, status, page: 1 }))}
        />
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="接入账号 API 不可用" description={error} /> : null}
      <section className="control-table-frame">
        <Table<AccessAccount>
          columns={columns}
          dataSource={rows}
          loading={loading}
          rowKey="access_account_id"
          pagination={{ current: query.page, pageSize: query.pageSize, total, showSizeChanger: true, onChange: (page, pageSize) => setQuery((prev) => ({ ...prev, page, pageSize })) }}
          scroll={{ x: 'max-content' }}
        />
      </section>
    </div>
  );
}
