import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Alert,
  Button,
  Card,
  Col,
  Divider,
  Form,
  Input,
  InputNumber,
  Row,
  Space,
  Spin,
  Switch,
  Tabs,
  Tag,
  Typography,
  Popconfirm,
  message,
} from 'antd';
import { LockOutlined, ReloadOutlined, SafetyOutlined, UserOutlined } from '@ant-design/icons';
import {
  applySystemConfig,
  getConfigSchema,
  getSystemConfigs,
  updateConfigSection,
  validateConfigSection,
  type ConfigSchemaField,
  type SystemConfigsResponse,
} from '../api';

const { Title, Text } = Typography;

type ConfigGroup = 'service' | 'core' | 'auth';
type DraftValue = string | number | boolean;
type DraftState = Record<string, DraftValue>;

const SERVICE_GROUP_LABELS: Record<string, string> = {
  listener: '监听与登录',
  server: '基站连接策略',
  client: '用户连接策略',
  http_api: 'HTTP API',
};

function getValueByPath(source: Record<string, unknown> | undefined, path: string[]): unknown {
  let current: unknown = source;
  for (const segment of path) {
    if (!current || typeof current !== 'object' || !(segment in (current as Record<string, unknown>))) {
      return undefined;
    }
    current = (current as Record<string, unknown>)[segment];
  }
  return current;
}

function setValueByPath(target: Record<string, unknown>, path: string[], value: unknown) {
  let current: Record<string, unknown> = target;
  path.forEach((segment, index) => {
    if (index === path.length - 1) {
      current[segment] = value;
      return;
    }
    if (!current[segment] || typeof current[segment] !== 'object') {
      current[segment] = {};
    }
    current = current[segment] as Record<string, unknown>;
  });
}

function flattenConfig(
  schema: Record<string, ConfigSchemaField>,
  group: Exclude<ConfigGroup, 'auth'>,
  source: Record<string, unknown> | undefined,
): DraftState {
  const result: DraftState = {};
  Object.entries(schema).forEach(([key, meta]) => {
    if (meta.group !== group) return;
    const value = getValueByPath(source, meta.path);
    if (value !== undefined) {
      result[key] = value as DraftValue;
    } else if (meta.default !== undefined) {
      result[key] = meta.default as DraftValue;
    }
  });
  return result;
}

function inflateConfig(
  schema: Record<string, ConfigSchemaField>,
  group: Exclude<ConfigGroup, 'auth'>,
  draft: DraftState,
): Record<string, unknown> {
  const result: Record<string, unknown> = {};
  Object.entries(schema).forEach(([key, meta]) => {
    if (meta.group !== group || !(key in draft)) return;
    setValueByPath(result, meta.path, draft[key]);
  });
  return result;
}

function getErrorMessage(error: unknown, fallback: string): string {
  const response = (error as { response?: { data?: { error?: string; errors?: Array<{ message?: string }> } } })?.response;
  return response?.data?.errors?.[0]?.message || response?.data?.error || fallback;
}

function validateStrongPassword(_: unknown, value: string) {
  if (!value) {
    return Promise.reject(new Error('请输入新密码'));
  }
  if (value.length < 8) {
    return Promise.reject(new Error('密码长度至少需要 8 位'));
  }
  if (!/[A-Z]/.test(value) || !/[a-z]/.test(value) || !/[0-9]/.test(value)) {
    return Promise.reject(new Error('密码需包含大写字母、小写字母和数字'));
  }
  return Promise.resolve();
}

const Settings: React.FC = () => {
  const [authForm] = Form.useForm();
  const [loading, setLoading] = useState(true);
  const [savingSection, setSavingSection] = useState<ConfigGroup | null>(null);
  const [applyLoading, setApplyLoading] = useState(false);
  const [schema, setSchema] = useState<Record<string, ConfigSchemaField>>({});
  const [configs, setConfigs] = useState<SystemConfigsResponse>({});
  const [serviceDraft, setServiceDraft] = useState<DraftState>({});
  const [coreDraft, setCoreDraft] = useState<DraftState>({});
  const [currentUser, setCurrentUser] = useState('admin');

  const loadData = useCallback(async () => {
    setLoading(true);
    try {
      const [configData, schemaData] = await Promise.all([getSystemConfigs(), getConfigSchema()]);
      setConfigs(configData);
      setSchema(schemaData);
      setServiceDraft(flattenConfig(schemaData, 'service', configData.service));
      setCoreDraft(flattenConfig(schemaData, 'core', configData.core));
      const adminUser = configData.auth?.admin_user || 'admin';
      setCurrentUser(adminUser);
      authForm.setFieldsValue({ username: adminUser, old_password: '', new_password: '', confirm_password: '' });
    } catch {
      message.error('加载系统配置失败');
    } finally {
      setLoading(false);
    }
  }, [authForm]);

  useEffect(() => {
    loadData();
  }, [loadData]);

  const serviceGroups = useMemo(() => {
    const grouped: Record<string, Array<[string, ConfigSchemaField]>> = {};
    Object.entries(schema).forEach(([key, meta]) => {
      if (meta.group !== 'service') return;
      const segment = meta.path[0] || 'other';
      if (!grouped[segment]) grouped[segment] = [];
      grouped[segment].push([key, meta]);
    });
    return grouped;
  }, [schema]);

  const coreFields = useMemo(
    () => Object.entries(schema).filter(([, meta]) => meta.group === 'core'),
    [schema],
  );

  const updateDraftValue = (group: 'service' | 'core', key: string, value: DraftValue) => {
    const setter = group === 'service' ? setServiceDraft : setCoreDraft;
    setter((prev) => ({ ...prev, [key]: value }));
  };

  const resetSection = (group: 'service' | 'core') => {
    const source = group === 'service' ? configs.service : configs.core;
    const setter = group === 'service' ? setServiceDraft : setCoreDraft;
    setter(flattenConfig(schema, group, source));
  };

  const saveSection = async (group: 'service' | 'core') => {
    const draft = group === 'service' ? serviceDraft : coreDraft;
    const payload = inflateConfig(schema, group, draft);
    setSavingSection(group);
    try {
      await validateConfigSection(group, payload);
      await updateConfigSection(group, payload);
      message.success(group === 'service' ? '服务配置已保存并广播' : '核心配置已保存并广播');
      await loadData();
    } catch (error) {
      message.error(getErrorMessage(error, '保存配置失败'));
    } finally {
      setSavingSection(null);
    }
  };

  const handleApply = async () => {
    setApplyLoading(true);
    try {
      await applySystemConfig();
      message.success('已通知所有节点重新加载配置');
    } catch (error) {
      message.error(getErrorMessage(error, '应用配置失败'));
    } finally {
      setApplyLoading(false);
    }
  };

  const handleAuthSubmit = async () => {
    try {
      const values = await authForm.validateFields();
      if (values.new_password !== values.confirm_password) {
        message.error('两次输入的密码不一致');
        return;
      }

      setSavingSection('auth');
      await updateConfigSection('auth', {
        old_password: values.old_password,
        admin_user: values.username || currentUser,
        admin_password: values.new_password,
      });
      message.success('认证配置已更新');
      await loadData();
    } catch (error) {
      if ((error as { errorFields?: unknown[] })?.errorFields) return;
      message.error(getErrorMessage(error, '更新认证配置失败'));
    } finally {
      setSavingSection(null);
    }
  };

  const renderField = (fieldKey: string, meta: ConfigSchemaField, value: DraftValue, group: 'service' | 'core') => {
    const restartTag = meta.restart_required ? <Tag color="orange">需重启</Tag> : <Tag color="green">热更新</Tag>;
    const control = meta.type === 'boolean' ? (
      <Switch checked={Boolean(value)} onChange={(checked) => updateDraftValue(group, fieldKey, checked)} />
    ) : meta.type === 'number' ? (
      <InputNumber
        style={{ width: '100%' }}
        min={meta.min}
        max={meta.max}
        value={typeof value === 'number' ? value : Number(value || 0)}
        onChange={(nextValue) => updateDraftValue(group, fieldKey, Number(nextValue ?? meta.default ?? 0))}
      />
    ) : (
      <Input value={String(value ?? '')} onChange={(event) => updateDraftValue(group, fieldKey, event.target.value)} />
    );

    return (
      <Col xs={24} md={12} key={fieldKey}>
        <Card size="small" style={{ borderColor: '#2e3450', height: '100%' }}>
          <Space direction="vertical" size={8} style={{ width: '100%' }}>
            <Space>
              <Text strong>{meta.label}</Text>
              {restartTag}
            </Space>
            {control}
            {(meta.min !== undefined || meta.max !== undefined) && (
              <Text type="secondary">范围：{meta.min ?? '-'} ~ {meta.max ?? '-'}</Text>
            )}
          </Space>
        </Card>
      </Col>
    );
  };

  if (loading) {
    return <Spin size="large" />;
  }

  return (
    <div>
      <Title level={4} style={{ marginBottom: 20 }}>系统设置</Title>
      <Alert
        type="info"
        showIcon
        style={{ marginBottom: 16 }}
        message="服务配置和核心配置保存后会自动广播到集群；标记为“需重启”的字段会先写入 Redis，待节点重启后完全生效。"
      />

      <Tabs
        items={[
          {
            key: 'service',
            label: '服务配置',
            children: (
              <Space direction="vertical" size={16} style={{ width: '100%' }}>
                {Object.entries(serviceGroups).map(([segment, fields]) => (
                  <Card
                    key={segment}
                    title={SERVICE_GROUP_LABELS[segment] || segment}
                    style={{ borderColor: '#2e3450' }}
                    extra={segment === 'http_api' ? <Tag color="blue">主节点默认启用</Tag> : null}
                  >
                    <Row gutter={[16, 16]}>
                      {fields.map(([fieldKey, meta]) => renderField(fieldKey, meta, serviceDraft[fieldKey], 'service'))}
                    </Row>
                  </Card>
                ))}
                <Space>
                  <Button type="primary" loading={savingSection === 'service'} onClick={() => saveSection('service')}>保存服务配置</Button>
                  <Button icon={<ReloadOutlined />} onClick={() => resetSection('service')}>重置本页</Button>
                </Space>
              </Space>
            ),
          },
          {
            key: 'core',
            label: '核心配置',
            children: (
              <Space direction="vertical" size={16} style={{ width: '100%' }}>
                <Card title="核心控制" style={{ borderColor: '#2e3450' }}>
                  <Row gutter={[16, 16]}>
                    {coreFields.map(([fieldKey, meta]) => renderField(fieldKey, meta, coreDraft[fieldKey], 'core'))}
                  </Row>
                </Card>
                <Space>
                  <Button type="primary" loading={savingSection === 'core'} onClick={() => saveSection('core')}>保存核心配置</Button>
                  <Button icon={<ReloadOutlined />} onClick={() => resetSection('core')}>重置本页</Button>
                </Space>
              </Space>
            ),
          },
          {
            key: 'auth',
            label: '认证配置',
            children: (
              <Card title="Web 登录账号管理" style={{ maxWidth: 680, borderColor: '#2e3450' }}>
                <Text type="secondary">当前用户名：{currentUser}</Text>
                <Divider />
                <Form form={authForm} layout="vertical" onFinish={handleAuthSubmit}>
                  <Form.Item name="username" label="管理用户名" rules={[{ required: true, message: '请输入管理用户名' }]}>
                    <Input prefix={<UserOutlined />} placeholder="输入新的管理用户名" />
                  </Form.Item>
                  <Form.Item name="old_password" label="当前密码" rules={[{ required: true, message: '请输入当前密码' }]}>
                    <Input.Password prefix={<LockOutlined />} placeholder="输入当前密码以验证身份" />
                  </Form.Item>
                  <Form.Item name="new_password" label="新密码" rules={[{ validator: validateStrongPassword }]}>
                    <Input.Password prefix={<LockOutlined />} placeholder="输入新密码" />
                  </Form.Item>
                  <Form.Item name="confirm_password" label="确认密码" rules={[{ required: true, message: '请再次输入新密码' }]}>
                    <Input.Password prefix={<LockOutlined />} placeholder="再次输入新密码" />
                  </Form.Item>
                  <Form.Item>
                    <Button type="primary" htmlType="submit" loading={savingSection === 'auth'}>
                      保存认证配置
                    </Button>
                  </Form.Item>
                </Form>
              </Card>
            ),
          },
          {
            key: 'apply',
            label: '集群应用',
            children: (
              <Card title="配置广播与安全说明" style={{ borderColor: '#2e3450' }}>
                <Space direction="vertical" size={16} style={{ width: '100%' }}>
                  <Alert
                    type="warning"
                    showIcon
                    message="手动应用会再次向全部节点广播 CONFIG，用于确认 Redis 中的新配置被各节点重新加载。"
                  />
                  <Alert
                    type="success"
                    showIcon
                    message="配置写入与应用操作会进入审计日志；敏感认证信息不会通过 /api/config 暴露明文密码。"
                  />
                  <Row gutter={[16, 16]}>
                    <Col xs={24} md={12}>
                      <Card size="small" style={{ borderColor: '#2e3450' }}>
                        <Space direction="vertical">
                          <Space>
                            <SafetyOutlined />
                            <Text strong>集群配置应用</Text>
                          </Space>
                          <Text type="secondary">当你修改了 Redis 中的配置但需要再次触发全节点重载时，可手动执行。</Text>
                          <Popconfirm title="确认重新向所有节点广播配置？" onConfirm={handleApply}>
                            <Button type="primary" loading={applyLoading}>重新应用到所有节点</Button>
                          </Popconfirm>
                        </Space>
                      </Card>
                    </Col>
                    <Col xs={24} md={12}>
                      <Card size="small" style={{ borderColor: '#2e3450' }}>
                        <Space direction="vertical">
                          <Text strong>当前覆盖范围</Text>
                          <Text type="secondary">服务配置：监听、登录开关、HTTP 管理项。</Text>
                          <Text type="secondary">核心配置：状态上报、多连接策略、离线通知。</Text>
                          <Text type="secondary">认证配置：管理用户名与 Web 登录密码。</Text>
                        </Space>
                      </Card>
                    </Col>
                  </Row>
                </Space>
              </Card>
            ),
          },
        ]}
      />
    </div>
  );
};

export default Settings;
