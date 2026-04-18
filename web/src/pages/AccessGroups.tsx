import React, { useState } from 'react';
import { Table, Button, Modal, Form, Input, Switch, Typography, Space, Row, Col, message, Popconfirm } from 'antd';
import { PlusOutlined, DeleteOutlined, EditOutlined } from '@ant-design/icons';
import type { ColumnsType } from 'antd/es/table';
import { usePolling } from '../hooks/usePolling';
import { accessGroupsApi } from '../api';
import type { AccessGroup } from '../api/types';

const { Title } = Typography;

const AccessGroups: React.FC = () => {
  const { data, loading, refresh } = usePolling(() => accessGroupsApi.getAll(), 3000);
  const [modalOpen, setModalOpen] = useState(false);
  const [editing, setEditing] = useState<AccessGroup | null>(null);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);

  const dataSource = data
    ? Object.entries(data).map(([key, val]) => ({ ...val, key }))
    : [];

  const handleCreate = () => {
    setEditing(null);
    form.resetFields();
    form.setFieldsValue({
      nearest_mpt_enable: false,
      allow_visible_inside_group: true, allow_access_inside_group: true, allow_nearby_inside_group: true,
      allow_visible_outside_group: true, allow_access_outside_group: true, allow_nearby_outside_group: true,
    });
    setModalOpen(true);
  };

  const handleEdit = (record: AccessGroup) => {
    setEditing(record);
    form.setFieldsValue(record);
    setModalOpen(true);
  };

  const handleDelete = async (uid: string) => {
    try {
      await accessGroupsApi.remove(uid);
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
        await accessGroupsApi.update(editing.uid, { ...editing, ...values });
        message.success('更新成功');
      } else {
        values.uid = values.group_name;
        await accessGroupsApi.create(values);
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

  const boolRender = (v: boolean) => v ? '是' : '否';

  const columns: ColumnsType<AccessGroup & { key: string }> = [
    { title: '分组名', dataIndex: 'group_name', key: 'group_name', sorter: (a, b) => a.group_name.localeCompare(b.group_name) },
    { title: '最近点', key: 'nearest_mpt_enable', render: (_, r) => boolRender(r.nearest_mpt_enable), width: 80 },
    { title: '最近点源', dataIndex: 'nearest_mpt_source_name', key: 'nearest_mpt_source_name' },
    { title: '组内可见', key: 'v_in', render: (_, r) => boolRender(r.allow_visible_inside_group), width: 80 },
    { title: '组内访问', key: 'a_in', render: (_, r) => boolRender(r.allow_access_inside_group), width: 80 },
    { title: '组外可见', key: 'v_out', render: (_, r) => boolRender(r.allow_visible_outside_group), width: 80 },
    { title: '组外访问', key: 'a_out', render: (_, r) => boolRender(r.allow_access_outside_group), width: 80 },
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
        <Title level={4} style={{ margin: 0 }}>访问控制分组</Title>
        <Button type="primary" icon={<PlusOutlined />} onClick={handleCreate}>新增分组</Button>
      </div>
      <Table columns={columns} dataSource={dataSource} loading={loading} size="small"
        pagination={{ pageSize: 20, showSizeChanger: true, showTotal: (t) => `共 ${t} 条` }}
        scroll={{ x: 900 }}
      />
      <Modal title={editing ? '编辑分组' : '新增分组'} open={modalOpen} onOk={handleSubmit}
        onCancel={() => setModalOpen(false)} confirmLoading={submitting} width={640}>
        <Form form={form} layout="vertical" size="small">
          <Row gutter={16}>
            <Col span={12}>
              <Form.Item name="group_name" label="分组名称" rules={[{ required: true }]}>
                <Input disabled={!!editing} />
              </Form.Item>
            </Col>
            <Col span={12}>
              <Form.Item name="nearest_mpt_source_name" label="最近点源名称">
                <Input />
              </Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={8}>
              <Form.Item name="nearest_mpt_enable" label="启用最近点" valuePropName="checked"><Switch /></Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="allow_visible_inside_group" label="组内可见" valuePropName="checked"><Switch /></Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="allow_access_inside_group" label="组内访问" valuePropName="checked"><Switch /></Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={8}>
              <Form.Item name="allow_nearby_inside_group" label="组内最近检索" valuePropName="checked"><Switch /></Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="allow_visible_outside_group" label="组外可见" valuePropName="checked"><Switch /></Form.Item>
            </Col>
            <Col span={8}>
              <Form.Item name="allow_access_outside_group" label="组外访问" valuePropName="checked"><Switch /></Form.Item>
            </Col>
          </Row>
          <Row gutter={16}>
            <Col span={8}>
              <Form.Item name="allow_nearby_outside_group" label="组外最近检索" valuePropName="checked" style={{ marginBottom: 0 }}><Switch /></Form.Item>
            </Col>
          </Row>
        </Form>
      </Modal>
    </div>
  );
};

export default AccessGroups;
