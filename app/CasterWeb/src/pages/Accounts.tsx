import React, { useState } from 'react';
import { Table, Button, Modal, Form, Input, InputNumber, Select, Typography, Space, Tag, message, Popconfirm } from 'antd';
import { PlusOutlined, DeleteOutlined, EditOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { usePolling } from '../hooks/usePolling';
import { accountsApi } from '../api';
import type { AccountRecord } from '../api/types';
import { AccountType, AccountStateType, AccountActiveState } from '../api/types';
import { getLocalTime } from '../utils/format';

const { Title } = Typography;

const typeLabels: Record<number, string> = {
  [AccountType.ACCOUNT_TYPE_UNKNOWN]: '未知',
  [AccountType.ACCOUNT_TYPE_LONG_TERM]: '长期',
  [AccountType.ACCOUNT_TYPE_EXPIRE_BY_DATE]: '期限',
  [AccountType.ACCOUNT_TYPE_EXPIRE_BY_USAGE]: '时限',
};

const stateLabels: Record<number, { text: string; color: string }> = {
  [AccountStateType.ACCOUNT_STATE_TYPE_NORMAL]: { text: '已启用', color: 'green' },
  [AccountStateType.ACCOUNT_STATE_TYPE_FROZEN]: { text: '已停用', color: 'orange' },
  [AccountStateType.ACCOUNT_STATE_TYPE_EXPIRED]: { text: '已过期', color: 'red' },
};

const activeLabels: Record<number, { text: string; color: string }> = {
  [AccountActiveState.ACCOUNT_ACTIVE_STATE_ACTIVE]: { text: '已激活', color: 'green' },
  [AccountActiveState.ACCOUNT_ACTIVE_STATE_INACTIVE]: { text: '未激活', color: 'default' },
};

const Accounts: React.FC = () => {
  const { data, loading, refresh } = usePolling(() => accountsApi.getAll(), 3000);
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<AccountRecord | null>(null);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  const handleCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({ type: AccountType.ACCOUNT_TYPE_LONG_TERM, state: AccountStateType.ACCOUNT_STATE_TYPE_NORMAL, connection_limit: 0 });
    setModalOpen(true);
  };

  const handleEdit = (record: AccountRecord) => {
    setEditing(record);
    form.setFieldsValue(record);
    setModalOpen(true);
  };

  const handleDelete = async (uid: string) => {
    try {
      await accountsApi.remove(uid);
      message.success('删除成功');
      refresh();
    } catch {
      message.error('删除失败');
    }
  };

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields();
      setSubmitting(true);
      if (editing) {
        await accountsApi.update(editing.uid, { ...editing, ...values });
        message.success('更新成功');
      } else {
        values.uid = values.account;
        await accountsApi.create(values);
        message.success('创建成功');
      }
      setModalOpen(false);
      refresh();
    } catch {
      message.error('操作失败');
    } finally {
      setSubmitting(false);
    }
  };

  const columns: ColumnsType<AccountRecord & { key: string }> = [
    { title: '账号', dataIndex: 'account', key: 'account', width: 120, sorter: (a, b) => a.account.localeCompare(b.account) },
    { title: '用户名/机构名', dataIndex: 'contact_name', key: 'contact_name' },
    { title: '支持连接数', dataIndex: 'connection_limit', key: 'connection_limit', width: 100, render: (v) => v === 0 ? '不限' : v },
    { title: '账号类型', key: 'type', render: (_, r) => typeLabels[r.type] || '未知' },
    {
      title: '启用状态', key: 'state',
      render: (_, r) => {
        const s = stateLabels[r.state] || { text: '未知', color: 'default' };
        return <Tag color={s.color}>{s.text}</Tag>;
      },
    },
    {
      title: '激活状态', key: 'active',
      render: (_, r) => {
        const a = activeLabels[r.active] || { text: '未知', color: 'default' };
        return <Tag color={a.color}>{a.text}</Tag>;
      },
    },
    { title: '访问组', dataIndex: 'group_uid', key: 'group_uid' },
    { title: '注册日期', key: 'register_time', width: 180, render: (_, r) => getLocalTime(r.register_time) },
    { title: '修改日期', key: 'update_time', width: 180, render: (_, r) => getLocalTime(r.update_time) },
    { title: '备注', dataIndex: 'remark', key: 'remark', ellipsis: true },
    {
      title: '操作', key: 'action', width: 140,
      render: (_, record) => (
        <Space>
          <Button type="link" size="small" icon={<EditOutlined />} onClick={() => handleEdit(record)}>编辑</Button>
          <Popconfirm title="确定删除？" onConfirm={() => handleDelete(record.uid)}>
            <Button type="link" size="small" danger icon={<DeleteOutlined />}>删除</Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
        <Title level={4} style={{ margin: 0 }}>账户管理</Title>
        <Button type="primary" icon={<PlusOutlined />} onClick={handleCreate}>新增账户</Button>
      </div>
      <Table columns={columns} dataSource={dataSource} loading={loading} size="small"
        pagination={{ pageSize: 20, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
        scroll={{ x: 1100 }}
      />
      <Modal title={editing ? '编辑账户' : '新增账户'} open={modalOpen} onOk={handleSubmit}
        onCancel={() => setModalOpen(false)} confirmLoading={submitting} width={600}>
        <Form form={form} layout="vertical">
          <Form.Item name="account" label="账号" rules={[{ required: true }]}>
            <Input disabled={!!editing} />
          </Form.Item>
          <Form.Item name="password" label="密码" rules={editing ? [] : [{ required: true }]}>
            <Input.Password />
          </Form.Item>
          <Form.Item name="contact_name" label="联系人/公司">
            <Input />
          </Form.Item>
          <Form.Item name="type" label="类型">
            <Select options={Object.entries(typeLabels).map(([k, v]) => ({ value: Number(k), label: v }))} />
          </Form.Item>
          <Form.Item name="state" label="状态">
            <Select options={Object.entries(stateLabels).map(([k, v]) => ({ value: Number(k), label: v.text }))} />
          </Form.Item>
          <Form.Item name="connection_limit" label="连接数限制 (0=不限)">
            <InputNumber min={0} style={{ width: '100%' }} />
          </Form.Item>
          <Form.Item name="group_uid" label="分组 UID">
            <Input />
          </Form.Item>
          <Form.Item name="remark" label="备注">
            <Input.TextArea rows={2} />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
};

export default Accounts;
