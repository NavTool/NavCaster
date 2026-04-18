import React, { useState } from 'react';
import { Form, Input, Button, Card, Typography, message, InputNumber, Space } from 'antd';
import { UserOutlined, LockOutlined, ApiOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { login } from '../api/auth';
import { setBaseURL, getBaseURL } from '../api/client';

const { Title } = Typography;

const Login: React.FC = () => {
  const [loading, setLoading] = useState(false);
  const navigate = useNavigate();

  const savedURL = getBaseURL();
  const defaultHost = savedURL ? new URL(savedURL).hostname : '127.0.0.1';
  const defaultPort = savedURL ? parseInt(new URL(savedURL).port) || 8080 : 8080;

  const onFinish = async (values: { host: string; port: number; username: string; password: string }) => {
    setLoading(true);
    try {
      const baseUrl = `http://${values.host}:${values.port}`;
      setBaseURL(baseUrl);
      await login(values.username, values.password);
      message.success('登录成功');
      navigate('/dashboard');
    } catch (error: any) {
      const retryAfter = error?.response?.data?.retry_after;
      if (error?.response?.status === 429 && retryAfter) {
        message.error(`登录尝试过于频繁，请在 ${retryAfter} 秒后重试`);
      } else {
        message.error('登录失败，请检查地址和凭据');
      }
    } finally {
      setLoading(false);
    }
  };

  return (
    <div style={{
      display: 'flex', justifyContent: 'center', alignItems: 'center',
      minHeight: '100vh', background: '#141625',
    }}>
      <Card style={{ width: 420, background: '#1e2235', border: '1px solid #2e3450' }}>
        <div style={{ textAlign: 'center', marginBottom: 32 }}>
          <div style={{
            width: 48, height: 48, borderRadius: 12, background: '#4a8eff',
            display: 'inline-flex', alignItems: 'center', justifyContent: 'center',
            color: '#fff', fontWeight: 700, fontSize: 20, marginBottom: 12,
          }}>NC</div>
          <Title level={3} style={{ margin: 0 }}>
            NavCaster 管理平台
          </Title>
          <div style={{ color: '#6b7194', fontSize: 13, marginTop: 4 }}>
            GNSS 差分数据基础设施管理
          </div>
        </div>
        <Form onFinish={onFinish} initialValues={{ host: defaultHost, port: defaultPort, username: 'admin', password: '' }} size="large">
          <Form.Item label="服务器" style={{ marginBottom: 16 }}>
            <Space.Compact style={{ width: '100%' }}>
              <Form.Item name="host" noStyle rules={[{ required: true, message: '请输入地址' }]}>
                <Input prefix={<ApiOutlined />} placeholder="地址" style={{ width: '70%' }} />
              </Form.Item>
              <Form.Item name="port" noStyle rules={[{ required: true, message: '端口' }]}>
                <InputNumber placeholder="端口" min={1} max={65535} style={{ width: '30%' }} />
              </Form.Item>
            </Space.Compact>
          </Form.Item>
          <Form.Item name="username" rules={[{ required: true, message: '请输入用户名' }]}>
            <Input prefix={<UserOutlined />} placeholder="用户名" />
          </Form.Item>
          <Form.Item name="password" rules={[{ required: true, message: '请输入密码' }]}>
            <Input.Password prefix={<LockOutlined />} placeholder="密码" />
          </Form.Item>
          <Form.Item>
            <Button type="primary" htmlType="submit" loading={loading} block>
              登录
            </Button>
          </Form.Item>
        </Form>
      </Card>
    </div>
  );
};

export default Login;
