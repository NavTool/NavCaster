import { SaveOutlined } from '@ant-design/icons';
import { Alert, Button, Descriptions, Form, Input, message } from 'antd';
import { useEffect, useState } from 'react';
import { apiErrorMessage, getProfile, updateProfile, type Account } from '../../api/identity';
import { AccountStatusTag, RoleTag } from './IdentityBadges';
import { formatIdentityTime } from './identityFormat';

export default function ProfilePage() {
  const [profile, setProfile] = useState<Account | null>(null);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);
  const [form] = Form.useForm();

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    getProfile()
      .then((account) => {
        if (cancelled) return;
        setProfile(account);
        form.setFieldsValue({
          display_name: account.display_name,
          email: account.email,
          phone: account.phone,
        });
        setError('');
      })
      .catch((err) => {
        if (!cancelled) setError(apiErrorMessage(err));
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => { cancelled = true; };
  }, [form]);

  async function submit(values: { display_name: string; email?: string; phone?: string }) {
    try {
      const updated = await updateProfile(values);
      setProfile(updated);
      message.success('个人资料已保存');
    } catch (err) {
      message.error(apiErrorMessage(err));
    }
  }

  return (
    <div className="identity-page">
      {error ? <Alert className="control-table-alert" type="error" showIcon message="个人资料接口不可用" description={error} /> : null}
      <section className="identity-profile-grid">
        <section className="control-panel">
          <h2>账号信息</h2>
          <Descriptions column={1} size="small" bordered>
            <Descriptions.Item label="用户名">{profile?.username ?? '-'}</Descriptions.Item>
            <Descriptions.Item label="账号 ID">{profile?.account_id ?? '-'}</Descriptions.Item>
            <Descriptions.Item label="角色">{profile ? <RoleTag role={profile.role} /> : '-'}</Descriptions.Item>
            <Descriptions.Item label="状态">{profile ? <AccountStatusTag status={profile.status} /> : '-'}</Descriptions.Item>
            <Descriptions.Item label="创建时间">{formatIdentityTime(profile?.created_at)}</Descriptions.Item>
          </Descriptions>
        </section>
        <section className="control-panel">
          <h2>资料编辑</h2>
          <Form form={form} layout="vertical" requiredMark={false} disabled={loading} onFinish={submit}>
            <Form.Item name="display_name" label="显示名" rules={[{ required: true, message: '请输入显示名' }]}>
              <Input />
            </Form.Item>
            <Form.Item name="email" label="邮箱">
              <Input />
            </Form.Item>
            <Form.Item name="phone" label="手机号">
              <Input />
            </Form.Item>
            <Button type="primary" htmlType="submit" icon={<SaveOutlined />}>保存资料</Button>
          </Form>
        </section>
      </section>
    </div>
  );
}
