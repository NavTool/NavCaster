import { DeleteOutlined, EditOutlined, KeyOutlined, PlusOutlined, ReloadOutlined } from '@ant-design/icons';
import { Alert, Button, Form, Input, InputNumber, Modal, Popconfirm, Select, Space, Table, message } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { useCallback, useEffect, useState } from 'react';
import {
  apiErrorMessage,
  createMyAccessAccount,
  deleteMyAccessAccount,
  listMyAccessAccounts,
  updateMyAccessAccount,
  updateMyAccessAccountPassword,
  updateMyAccessAccountStatus,
  type AccessAccount,
  type AccessAccountStatus,
} from '../../api/identity';
import { AccountStatusTag } from './IdentityBadges';
import { formatIdentityTime } from './identityFormat';

const statuses: Array<AccessAccountStatus | 'all'> = ['all', 'active', 'disabled', 'deleted'];

export default function UserAccessAccountsPage() {
  const [rows, setRows] = useState<AccessAccount[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [query, setQuery] = useState({ page: 1, pageSize: 10, search: '', status: 'all' as AccessAccountStatus | 'all' });
  const [editorOpen, setEditorOpen] = useState(false);
  const [editing, setEditing] = useState<AccessAccount | null>(null);
  const [passwordTarget, setPasswordTarget] = useState<AccessAccount | null>(null);
  const [form] = Form.useForm();
  const [passwordForm] = Form.useForm();

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const result = await listMyAccessAccounts(query);
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

  function openCreate() {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({ concurrency_limit: 1 });
    setEditorOpen(true);
  }

  function openEdit(row: AccessAccount) {
    setEditing(row);
    form.setFieldsValue({
      username: row.username,
      display_name: row.display_name,
      note: row.note,
      concurrency_limit: row.concurrency_limit,
    });
    setEditorOpen(true);
  }

  async function submit(values: { username?: string; password?: string; display_name: string; note?: string; concurrency_limit: number }) {
    try {
      if (editing) {
        await updateMyAccessAccount(editing.access_account_id, {
          display_name: values.display_name,
          note: values.note,
          concurrency_limit: values.concurrency_limit ?? 1,
          expires_at: null,
        });
        message.success('接入账号已更新');
      } else {
        await createMyAccessAccount({
          username: values.username ?? '',
          password: values.password ?? '',
          display_name: values.display_name,
          note: values.note,
          concurrency_limit: values.concurrency_limit ?? 1,
          expires_at: null,
        });
        message.success('接入账号已创建');
      }
      setEditorOpen(false);
      await refresh();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  async function changeStatus(row: AccessAccount, status: AccessAccountStatus) {
    try {
      await updateMyAccessAccountStatus(row.access_account_id, status);
      message.success('接入账号状态已更新');
      await refresh();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  async function submitPassword(values: { password: string }) {
    if (!passwordTarget) return;
    try {
      await updateMyAccessAccountPassword(passwordTarget.access_account_id, values.password);
      message.success('接入账号密码已更新');
      setPasswordTarget(null);
      passwordForm.resetFields();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  async function remove(row: AccessAccount) {
    try {
      await deleteMyAccessAccount(row.access_account_id);
      message.success('接入账号已删除');
      await refresh();
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

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
    { title: '显示名', dataIndex: 'display_name', render: (value) => value || '-' },
    { title: '备注', dataIndex: 'note', render: (value) => value || '-' },
    { title: '状态', dataIndex: 'status', render: (status) => <AccountStatusTag status={status} /> },
    { title: '并发限制', dataIndex: 'concurrency_limit', align: 'right', render: (value) => (value === 0 ? '不限' : value) },
    { title: '投影版本', dataIndex: 'projection_version', align: 'right', render: (value) => value ?? '-' },
    { title: '更新时间', dataIndex: 'updated_at', render: formatIdentityTime },
    {
      title: '操作',
      fixed: 'right',
      render: (_, row) => (
        <Space wrap size={4}>
          {row.status === 'active' ? <Button size="small" onClick={() => changeStatus(row, 'disabled')}>停用</Button> : null}
          {row.status === 'disabled' ? <Button size="small" onClick={() => changeStatus(row, 'active')}>启用</Button> : null}
          <Button size="small" icon={<EditOutlined />} disabled={row.status === 'deleted'} onClick={() => openEdit(row)}>编辑</Button>
          <Button size="small" icon={<KeyOutlined />} disabled={row.status === 'deleted'} onClick={() => setPasswordTarget(row)}>改密码</Button>
          {row.status !== 'deleted' ? (
            <Popconfirm title="删除后接入账号名不可复用，确认删除？" onConfirm={() => remove(row)}>
              <Button size="small" danger icon={<DeleteOutlined />}>删除</Button>
            </Popconfirm>
          ) : null}
        </Space>
      ),
    },
  ];

  return (
    <div className="identity-page">
      <div className="control-page-heading">
        <div>
          <h1>接入账号管理</h1>
          <p>接入账号用于设备、客户端或基站接入平台。Web 登录账号和接入账号相互独立。</p>
        </div>
        <Space wrap>
          <Button icon={<ReloadOutlined />} onClick={refresh}>刷新</Button>
          <Button type="primary" icon={<PlusOutlined />} onClick={openCreate}>创建接入账号</Button>
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

      <Modal title={editing ? '编辑接入账号' : '创建接入账号'} open={editorOpen} onCancel={() => setEditorOpen(false)} onOk={() => form.submit()} destroyOnClose>
        <Form form={form} layout="vertical" requiredMark={false} onFinish={submit}>
          <Form.Item name="username" label="接入账号名" rules={editing ? [] : [{ required: true, message: '请输入接入账号名' }]}>
            <Input disabled={Boolean(editing)} />
          </Form.Item>
          {!editing ? (
            <Form.Item name="password" label="接入密码" rules={[{ required: true, min: 8, message: '密码至少 8 位' }]}>
              <Input.Password />
            </Form.Item>
          ) : null}
          <Form.Item name="display_name" label="显示名" rules={[{ required: true, message: '请输入显示名' }]}>
            <Input />
          </Form.Item>
          <Form.Item name="note" label="备注">
            <Input />
          </Form.Item>
          <Form.Item name="concurrency_limit" label="并发限制">
            <InputNumber min={0} className="identity-number-input" />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title={`修改接入密码：${passwordTarget?.username ?? ''}`} open={Boolean(passwordTarget)} onCancel={() => setPasswordTarget(null)} onOk={() => passwordForm.submit()} destroyOnClose>
        <Form form={passwordForm} layout="vertical" requiredMark={false} onFinish={submitPassword}>
          <Form.Item name="password" label="新密码" rules={[{ required: true, min: 8, message: '密码至少 8 位' }]}>
            <Input.Password />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
}
