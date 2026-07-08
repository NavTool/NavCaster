import { DeleteOutlined, KeyOutlined, PlusOutlined, ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Form, Input, Modal, Popconfirm, Select, Space, Table, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import {
  apiErrorMessage,
  createAccount,
  deleteAccount,
  listAccounts,
  resetAccountPassword,
  updateAccountStatus,
  type Account,
  type AccountRole,
  type AccountStatus,
} from '../../api/identity';
import { ControlPaginationBar } from '../components/TablePage';
import { AccountStatusTag, RoleTag } from './IdentityBadges';
import { formatIdentityTime } from './identityFormat';
import { accountStatusLabel, createdViaLabel } from '../labels';

const accountStatuses: Array<AccountStatus | 'all'> = ['all', 'active', 'disabled', 'deleted'];

export default function AdminUsersPage() {
  const [rows, setRows] = useState<Account[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [query, setQuery] = useState({ page: 1, pageSize: 10, search: '', status: 'all' as AccountStatus | 'all', role: 'all' as AccountRole | 'all' });
  const [createOpen, setCreateOpen] = useState(false);
  const [passwordTarget, setPasswordTarget] = useState<Account | null>(null);
  const [createForm] = Form.useForm();
  const [passwordForm] = Form.useForm();

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await listAccounts(query);
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

  async function submitCreate(values: { username: string; password: string; display_name: string; role: AccountRole; status: AccountStatus }) {
    try {
      await createAccount(values);
      message.success('账号已创建');
      setCreateOpen(false);
      createForm.resetFields();
      await refresh();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  async function changeStatus(row: Account, status: AccountStatus) {
    try {
      await updateAccountStatus(row.account_id, status);
      message.success('账号状态已更新');
      await refresh();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  async function submitPassword(values: { password: string }) {
    if (!passwordTarget) return;
    try {
      await resetAccountPassword(passwordTarget.account_id, values.password, true);
      message.success('密码已重置');
      setPasswordTarget(null);
      passwordForm.resetFields();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  async function remove(row: Account) {
    try {
      await deleteAccount(row.account_id);
      message.success('账号已删除');
      await refresh();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  const columns: ColumnsType<Account> = [
    {
      title: '用户账号',
      dataIndex: 'username',
      fixed: 'left',
      width: 240,
      render: (_, row) => (
        <div className="control-primary-cell">
          <strong>{row.username}</strong>
          <span>{row.account_id}</span>
        </div>
      ),
    },
    { title: '显示名', dataIndex: 'display_name', render: (value) => value || '-' },
    { title: '角色', dataIndex: 'role', render: (role) => <RoleTag role={role} /> },
    { title: '状态', dataIndex: 'status', render: (status) => <AccountStatusTag status={status} /> },
    { title: '来源', dataIndex: 'created_via', render: createdViaLabel },
    { title: '接入账号', dataIndex: 'access_account_count', align: 'right', render: (value) => value ?? '-' },
    { title: '活跃会话', dataIndex: 'active_session_count', align: 'right', render: (value) => value ?? '-' },
    { title: '创建时间', dataIndex: 'created_at', render: formatIdentityTime },
    {
      title: '操作',
      fixed: 'right',
      width: 260,
      render: (_, row) => (
        <Space wrap size={4}>
          {row.status === 'active' ? <Button size="small" onClick={() => changeStatus(row, 'disabled')}>停用</Button> : null}
          {row.status === 'disabled' ? <Button size="small" onClick={() => changeStatus(row, 'active')}>启用</Button> : null}
          <Button size="small" icon={<KeyOutlined />} onClick={() => setPasswordTarget(row)}>重置密码</Button>
          {row.status !== 'deleted' ? (
            <Popconfirm title="删除后用户名不可复用，确认删除？" onConfirm={() => remove(row)}>
              <Button size="small" danger icon={<DeleteOutlined />}>删除</Button>
            </Popconfirm>
          ) : null}
        </Space>
      ),
    },
  ];

  return (
    <div className="identity-page">
      <section className="identity-toolbar">
        <Input.Search
          allowClear
          placeholder="搜索用户名或显示名"
          value={query.search}
          onChange={(event) => setQuery((prev) => ({ ...prev, search: event.target.value, page: 1 }))}
        />
        <Select
          value={query.status}
          options={accountStatuses.map((status) => ({ label: status === 'all' ? '全部状态' : accountStatusLabel(status), value: status }))}
          onChange={(status) => setQuery((prev) => ({ ...prev, status, page: 1 }))}
        />
        <Select
          value={query.role}
          options={[
            { label: '全部角色', value: 'all' },
            { label: '管理员', value: 'admin' },
            { label: '普通用户', value: 'user' },
          ]}
          onChange={(role) => setQuery((prev) => ({ ...prev, role, page: 1 }))}
        />
        <Space className="identity-toolbar-actions" wrap>
          <Button icon={<ReloadOutlined />} onClick={refresh}>刷新</Button>
          <Button type="primary" icon={<PlusOutlined />} onClick={() => setCreateOpen(true)}>创建用户</Button>
        </Space>
      </section>

      {error ? <Alert className="control-table-alert" type="error" showIcon message="用户账号接口不可用" description={error} /> : null}

      <section className="control-table-frame">
        <Table<Account>
          columns={columns}
          dataSource={rows}
          loading={loading}
          rowKey="account_id"
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

      <Modal title="创建用户账号" open={createOpen} onCancel={() => setCreateOpen(false)} onOk={() => createForm.submit()} destroyOnClose>
        <Form form={createForm} layout="vertical" requiredMark={false} initialValues={{ role: 'user', status: 'active' }} onFinish={submitCreate}>
          <Form.Item name="username" label="用户名" rules={[{ required: true, message: '请输入用户名' }]}>
            <Input />
          </Form.Item>
          <Form.Item name="display_name" label="显示名" rules={[{ required: true, message: '请输入显示名' }]}>
            <Input />
          </Form.Item>
          <Form.Item name="password" label="初始密码" rules={[{ required: true, min: 8, message: '密码至少 8 位' }]}>
            <Input.Password />
          </Form.Item>
          <Form.Item name="role" label="角色">
            <Select options={[{ label: '普通用户', value: 'user' }, { label: '管理员', value: 'admin' }]} />
          </Form.Item>
          <Form.Item name="status" label="状态">
            <Select options={[{ label: accountStatusLabel('active'), value: 'active' }, { label: accountStatusLabel('disabled'), value: 'disabled' }]} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title={`重置密码：${passwordTarget?.username ?? ''}`} open={Boolean(passwordTarget)} onCancel={() => setPasswordTarget(null)} onOk={() => passwordForm.submit()} destroyOnClose>
        <Form form={passwordForm} layout="vertical" requiredMark={false} onFinish={submitPassword}>
          <Form.Item name="password" label="新密码" rules={[{ required: true, min: 8, message: '密码至少 8 位' }]}>
            <Input.Password />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
