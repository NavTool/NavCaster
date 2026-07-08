import { ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Input, Select, Space, Table } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { apiErrorMessage, listAdminAccessAccounts, type AccessAccount, type AccessAccountStatus } from '../../api/identity';
import { ControlPaginationBar } from '../components/TablePage';
import { AccountStatusTag } from './IdentityBadges';
import { formatIdentityTime } from './identityFormat';
import { accountStatusLabel } from '../labels';

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
      width: 260,
      render: (_, row) => (
        <div className="control-primary-cell">
          <Link to={`/admin/control/access/${row.access_account_id}`}>{row.username}</Link>
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
      <section className="identity-toolbar">
        <Input.Search
          allowClear
          placeholder="搜索所有接入账号或显示名"
          value={query.search}
          onChange={(event) => setQuery((prev) => ({ ...prev, search: event.target.value, page: 1 }))}
        />
        <Select
          value={query.status}
          options={statuses.map((status) => ({ label: status === 'all' ? '全部状态' : accountStatusLabel(status), value: status }))}
          onChange={(status) => setQuery((prev) => ({ ...prev, status, page: 1 }))}
        />
        <Space className="identity-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} onClick={refresh}>刷新</Button>
        </Space>
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="接入账号接口不可用" description={error} /> : null}
      <section className="control-table-frame">
        <Table<AccessAccount>
          columns={columns}
          dataSource={rows}
          loading={loading}
          rowKey="access_account_id"
          pagination={false}
          scroll={{ x: 'max-content' }}
        />
      </section>
      <ControlPaginationBar
        current={query.page}
        pageSize={query.pageSize}
        total={total}
        onChange={(page, pageSize) => setQuery((prev) => ({ ...prev, page, pageSize }))}
      />
    </div>
  );
}
