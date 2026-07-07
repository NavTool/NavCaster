import { LockOutlined, LoginOutlined, UserOutlined } from '@ant-design/icons';
import { Alert, Button, Form, Input, message } from 'antd';
import { Link, useNavigate } from 'react-router-dom';
import { useState } from 'react';
import { apiErrorMessage, login } from '../../api/identity';

export default function LoginPage() {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const navigate = useNavigate();

  async function submit(values: { username: string; password: string }) {
    setLoading(true);
    setError('');
    try {
      const result = await login(values.username, values.password);
      message.success('登录成功');
      navigate(result.account.role === 'admin' ? '/admin/control/users' : '/admin/control/user/access-accounts', { replace: true });
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
        <h1>登录 NavCaster</h1>
        <p>使用用户账号进入 Web 控制台。设备接入请使用接入账号，不使用 Web token。</p>
        {error ? <Alert type="error" showIcon message={error} /> : null}
        <Form layout="vertical" requiredMark={false} onFinish={submit}>
          <Form.Item name="username" label="用户名" rules={[{ required: true, message: '请输入用户名' }]}>
            <Input prefix={<UserOutlined />} autoComplete="username" />
          </Form.Item>
          <Form.Item name="password" label="密码" rules={[{ required: true, message: '请输入密码' }]}>
            <Input.Password prefix={<LockOutlined />} autoComplete="current-password" />
          </Form.Item>
          <Button block type="primary" htmlType="submit" icon={<LoginOutlined />} loading={loading}>
            登录
          </Button>
        </Form>
        <div className="identity-entry-footer">
          <span>还没有账号？</span>
          <Link to="/register">自助注册</Link>
        </div>
      </section>
    </main>
  );
}
