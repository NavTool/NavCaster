import React, { useState, useEffect } from 'react';
import { Card, Form, Input, Button, Typography, message, Descriptions } from 'antd';
import { LockOutlined, UserOutlined } from '@ant-design/icons';
import api from '../api/client';

const { Title } = Typography;

const Settings: React.FC = () => {
  const [form] = Form.useForm();
  const [loading, setLoading] = useState(false);
  const [currentUser, setCurrentUser] = useState('admin');

  useEffect(() => {
    api.get('/api/config/auth').then(({ data }) => {
      if (data?.admin_user) setCurrentUser(data.admin_user);
    }).catch(() => {
      // Redis has no CONF:AUTH yet — use default
      setCurrentUser('admin');
    });
  }, []);

  const handleSubmit = async () => {
    try {
      const values = await form.validateFields();
      if (values.new_password !== values.confirm_password) {
        message.error('两次输入的密码不一致');
        return;
      }
      setLoading(true);
      await api.put('/api/config/auth', {
        old_password: values.old_password,
        admin_user: values.username || currentUser,
        admin_password: values.new_password,
      });
      message.success('密码修改成功，下次登录生效');
      form.resetFields();
      // Refresh current user display
      if (values.username) setCurrentUser(values.username);
    } catch (err: any) {
      const msg = err?.response?.data?.error || '修改失败';
      message.error(msg);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div>
      <Title level={4} style={{ marginBottom: 20 }}>系统设置</Title>

      <Card title="Web 登录账号管理" style={{ maxWidth: 500, borderColor: '#2e3450' }}>
        <Descriptions column={1} size="small" style={{ marginBottom: 24 }}>
          <Descriptions.Item label="当前用户名">{currentUser}</Descriptions.Item>
        </Descriptions>
        <Form form={form} layout="vertical" onFinish={handleSubmit}>
          <Form.Item name="username" label="新用户名（留空则不修改）">
            <Input prefix={<UserOutlined />} placeholder={currentUser} />
          </Form.Item>
          <Form.Item name="old_password" label="当前密码" rules={[{ required: true, message: '请输入当前密码' }]}>
            <Input.Password prefix={<LockOutlined />} placeholder="输入当前密码以验证身份" />
          </Form.Item>
          <Form.Item name="new_password" label="新密码" rules={[{ required: true, message: '请输入新密码' }]}>
            <Input.Password prefix={<LockOutlined />} placeholder="输入新密码" />
          </Form.Item>
          <Form.Item
            name="confirm_password"
            label="确认密码"
            rules={[{ required: true, message: '请再次输入密码' }]}
          >
            <Input.Password prefix={<LockOutlined />} placeholder="再次输入新密码" />
          </Form.Item>
          <Form.Item>
            <Button type="primary" htmlType="submit" loading={loading}>
              保存修改
            </Button>
          </Form.Item>
        </Form>
      </Card>
    </div>
  );
};

export default Settings;
