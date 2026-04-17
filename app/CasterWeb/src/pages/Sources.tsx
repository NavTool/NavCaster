import React, { useState } from 'react';
import { Table, Button, Modal, Form, Input, Select, Typography, Space, message, Popconfirm } from 'antd';
import { PlusOutlined, DeleteOutlined, EditOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { usePolling } from '../hooks/usePolling';
import { sourcesApi } from '../api';
import type { SourceRecord } from '../api/types';
import { SourceRecordType, SourceDecordType, SourceDisplayType } from '../api/types';

const { Title } = Typography;

const recordTypeLabels: Record<number, string> = {
  [SourceRecordType.SOURCE_RECORD_TYPE_REAL]: '实际挂载点',
  [SourceRecordType.SOURCE_RECORD_TYPE_NEAREST]: '最近挂载点',
  [SourceRecordType.SOURCE_RECORD_TYPE_ALIAS]: '别名',
  [SourceRecordType.SOURCE_RECORD_TYPE_PROXY]: '代理',
  [SourceRecordType.SOURCE_RECORD_TYPE_GRID]: '网格',
};

const displayTypeLabels: Record<number, string> = {
  [SourceDisplayType.SOURCE_DISP_TYPE_ALWAYS_SHOW]: '总是显示',
  [SourceDisplayType.SOURCE_DISP_TYPE_ALWAYS_HIDE]: '总是隐藏',
  [SourceDisplayType.SOURCE_DISP_TYPE_SHOW_WHEN_ONLINE]: '上线后显示',
};

const Sources: React.FC = () => {
  const { data, loading, refresh } = usePolling(() => sourcesApi.getAll(), 3000);
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<SourceRecord | null>(null);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  const handleCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({ record_type: SourceRecordType.SOURCE_RECORD_TYPE_REAL, display_type: SourceDisplayType.SOURCE_DISP_TYPE_ALWAYS_SHOW, decode_type: SourceDecordType.SOURCE_DECODE_TYPE_AUTO });
    setModalOpen(true);
  };

  const handleEdit = (record: SourceRecord) => {
    setEditing(record);
    form.setFieldsValue(record);
    setModalOpen(true);
  };

  const handleDelete = async (uid: string) => {
    try {
      await sourcesApi.remove(uid);
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
        await sourcesApi.update(editing.uid, { ...editing, ...values });
        message.success('更新成功');
      } else {
        values.uid = values.mountpoint;
        await sourcesApi.create(values);
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

  const columns: ColumnsType<SourceRecord & { key: string }> = [
    { title: '挂载点', dataIndex: 'mountpoint', key: 'mountpoint', sorter: (a, b) => a.mountpoint.localeCompare(b.mountpoint) },
    { title: '标识', dataIndex: 'identufier', key: 'identufier' },
    { title: '格式', dataIndex: 'format', key: 'format' },
    { title: '载波', dataIndex: 'carrier', key: 'carrier' },
    { title: '导航系统', dataIndex: 'nav_system', key: 'nav_system' },
    { title: '国家', dataIndex: 'country', key: 'country' },
    { title: '类型', key: 'record_type', render: (_, r) => recordTypeLabels[r.record_type] || '未知' },
    { title: '显示', key: 'display_type', render: (_, r) => displayTypeLabels[r.display_type] || '未知' },
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
        <Title level={4} style={{ margin: 0 }}>源表管理</Title>
        <Button type="primary" icon={<PlusOutlined />} onClick={handleCreate}>新增源</Button>
      </div>
      <Table columns={columns} dataSource={dataSource} loading={loading} size="small"
        pagination={{ pageSize: 20, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
        scroll={{ x: 1000 }}
      />
      <Modal title={editing ? '编辑源' : '新增源'} open={modalOpen} onOk={handleSubmit}
        onCancel={() => setModalOpen(false)} confirmLoading={submitting} width={600}>
        <Form form={form} layout="vertical">
          <Form.Item name="mountpoint" label="挂载点名称" rules={[{ required: true }]}>
            <Input disabled={!!editing} />
          </Form.Item>
          <Form.Item name="identufier" label="标识符"><Input /></Form.Item>
          <Form.Item name="format" label="数据格式"><Input /></Form.Item>
          <Form.Item name="carrier" label="载波"><Input /></Form.Item>
          <Form.Item name="nav_system" label="导航系统"><Input /></Form.Item>
          <Form.Item name="country" label="国家"><Input /></Form.Item>
          <Form.Item name="latitude" label="纬度"><Input /></Form.Item>
          <Form.Item name="longitude" label="经度"><Input /></Form.Item>
          <Form.Item name="record_type" label="类型">
            <Select options={Object.entries(recordTypeLabels).map(([k, v]) => ({ value: Number(k), label: v }))} />
          </Form.Item>
          <Form.Item name="display_type" label="显示类型">
            <Select options={Object.entries(displayTypeLabels).map(([k, v]) => ({ value: Number(k), label: v }))} />
          </Form.Item>
        </Form>
      </Modal>
    </div>
  );
};

export default Sources;
