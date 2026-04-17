import React, { useState } from 'react';
import { Table, Button, Modal, Form, Input, InputNumber, Select, Typography, Space, message, Popconfirm } from 'antd';
import { PlusOutlined, DeleteOutlined, EditOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { usePolling } from '../hooks/usePolling';
import { pullRecordsApi, pullStatesApi } from '../api';
import type { PullRecord } from '../api/types';
import { PullType } from '../api/types';

const { Title } = Typography;

const typeLabels: Record<number, string> = {
  [PullType.PULL_TYPE_NTRIP_1_0]: 'NTRIP 1.0',
  [PullType.PULL_TYPE_NTRIP_2_0]: 'NTRIP 2.0',
  [PullType.PULL_TYPE_TCP_CLIENT]: 'TCP Client',
  [PullType.PULL_TYPE_TCP_SERVER]: 'TCP Server',
};

const PullRelay: React.FC = () => {
  const { data, loading, refresh } = usePolling(() => pullRecordsApi.getAll(), 3000);
  const { data: states } = usePolling(() => pullStatesApi.getAll(), 3000);
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<PullRecord | null>(null);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  const handleCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({ type: PullType.PULL_TYPE_NTRIP_1_0, target_port: 2101 });
    setModalOpen(true);
  };

  const handleEdit = (record: PullRecord) => {
    setEditing(record);
    form.setFieldsValue(record);
    setModalOpen(true);
  };

  const handleDelete = async (uid: string) => {
    try {
      await pullRecordsApi.remove(uid);
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
        await pullRecordsApi.update(editing.uid, { ...editing, ...values });
        message.success('更新成功');
      } else {
        values.uid = `${values.target_ip}:${values.target_port}/${values.target_mpt}`;
        await pullRecordsApi.create(values);
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

  const columns: ColumnsType<PullRecord & { key: string }> = [
    { title: '本地挂载点', dataIndex: 'login_mpt', key: 'login_mpt' },
    { title: '类型', key: 'type', render: (_, r) => typeLabels[r.type] || '未知' },
    { title: '目标 IP', dataIndex: 'target_ip', key: 'target_ip' },
    { title: '目标端口', dataIndex: 'target_port', key: 'target_port' },
    { title: '目标挂载点', dataIndex: 'target_mpt', key: 'target_mpt' },
    { title: '账户', dataIndex: 'target_account', key: 'target_account' },
    {
      title: '状态', key: 'state',
      render: (_, r) => states && states[r.uid] ? '运行中' : '-',
    },
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
        <Title level={4} style={{ margin: 0 }}>Pull 中继</Title>
        <Button type="primary" icon={<PlusOutlined />} onClick={handleCreate}>新增 Pull</Button>
      </div>
      <Table columns={columns} dataSource={dataSource} loading={loading} size="small"
        pagination={{ pageSize: 20, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
        scroll={{ x: 900 }}
      />
      <Modal title={editing ? '编辑 Pull' : '新增 Pull'} open={modalOpen} onOk={handleSubmit}
        onCancel={() => setModalOpen(false)} confirmLoading={submitting} width={500}>
        <Form form={form} layout="vertical">
          <Form.Item name="login_mpt" label="本地挂载点" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          <Form.Item name="type" label="协议类型">
            <Select options={Object.entries(typeLabels).map(([k, v]) => ({ value: Number(k), label: v }))} />
          </Form.Item>
          <Form.Item name="target_ip" label="目标 IP" rules={[{ required: true }]}>
            <Input />
          </Form.Item>
          <Form.Item name="target_port" label="目标端口" rules={[{ required: true }]}>
            <InputNumber min={1} max={65535} style={{ width: '100%' }} />
          </Form.Item>
          <Form.Item name="target_mpt" label="目标挂载点">
            <Input />
          </Form.Item>
          <Form.Item name="target_account" label="账户">
            <Input />
          </Form.Item>
          <Form.Item name="target_password" label="密码">
            <Input.Password />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
};

export default PullRelay;
