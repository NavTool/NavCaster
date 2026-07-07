import { LockOutlined, UserAddOutlined, UserOutlined } from '@ant-design/icons';
import { Alert, Button, Form, Input, message } from 'antd';
import { Link, useNavigate } from 'react-router-dom';
import { useState } from 'react';
import { apiErrorMessage, login, registerAccount } from '../../api/identity';

export default function RegisterPage() {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const navigate = useNavigate();

  async function submit(values: { username: string; display_name: string; password: string; confirm_password: string }) {
    setLoading(true);
    setError('');
    try {
      await registerAccount({ username: values.username, display_name: values.display_name, password: values.password });
      await login(values.username, values.password);
      message.success('注册成功');
      navigate('/admin/control/user/access-accounts', { replace: true });
    } catch (err) {
      setError(apiErrorMessage(err));
    } finally {
      setLoading(false);
    }
  }

  return (
    <main className="identity-entry">
      <section className="identity-entry-panel">
        <div className="identity-brand-mark">NC</div>
        <h1>注册用户账号</h1>
        <p>自助注册会创建普通用户账号，登录后可以管理自己的接入账号。</p>
        {error ? <Alert type="error" showIcon message={error} /> : null}
        <Form layout="vertical" requiredMark={false} onFinish={submit}>
          <Form.Item name="username" label="用户名" rules={[{ required: true, message: '请输入用户名' }]}>
            <Input prefix={<UserOutlined />} autoComplete="username" />
          </Form.Item>
          <Form.Item name="display_name" label="显示名" rules={[{ required: true, message: '请输入显示名' }]}>
            <Input autoComplete="name" />
          </Form.Item>
          <Form.Item name="password" label="密码" rules={[{ required: true, min: 8, message: '密码至少 8 位' }]}>
            <Input.Password prefix={<LockOutlined />} autoComplete="new-password" />
          </Form.Item>
          <Form.Item
            name="confirm_password"
            label="确认密码"
            dependencies={['password']}
            rules={[
              { required: true, message: '请再次输入密码' },
              ({ getFieldValue }) => ({
                validator(_, value) {
                  return !value || getFieldValue('password') === value ? Promise.resolve() : Promise.reject(new Error('两次密码不一致'));
                },
              }),
            ]}
          >
            <Input.Password prefix={<LockOutlined />} autoComplete="new-password" />
          </Form.Item>
          <Button block type="primary" htmlType="submit" icon={<UserAddOutlined />} loading={loading}>
            注册并登录
          </Button>
        </Form>
        <div className="identity-entry-footer">
          <span>已有账号？</span>
          <Link to="/login">返回登录</Link>
        </div>
      </section>
    </main>
  );
}
