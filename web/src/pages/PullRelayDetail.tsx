import React from 'react';
import { Breadcrumb, Button, Card, Col, Descriptions, Empty, Row, Space, Statistic, Tag, Typography } from 'antd';
import { Link, useParams, useNavigate } from 'react-router-dom';
import { usePolling } from '../hooks/usePolling';
import { pullRecordsApi, pullStatesApi, relayStart, relayStop } from '../api';
import type { PullRecord, PullState } from '../api/types';
import { formatOnlineTime } from '../utils/format';

const { Title } = Typography;

const PullRelayDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const navigate = useNavigate();
  const { data: record, refresh: refreshRecord } = usePolling<PullRecord | null>(
    async () => (id ? pullRecordsApi.getOne(id) : null),
    3000,
  );
  const { data: stateMap, refresh: refreshState } = usePolling(() => pullStatesApi.getAll(), 3000);

  const relayState: PullState | undefined = id ? stateMap?.[id] : undefined;
  const running = !!relayState && relayState.state === 1;

  const handleToggle = async () => {
    if (!id) return;
    if (running) await relayStop('pull', id);
    else await relayStart('pull', id);
    refreshRecord();
    refreshState();
  };

  if (!id || !record) {
    return <Card style={{ borderColor: '#2e3450' }}><Empty description="未找到 Pull 中继" /></Card>;
  }

  return (
    <div>
      <Breadcrumb items={[{ title: <Link to="/relay/pull">Pull 中继</Link> }, { title: record.login_mpt || id }]} style={{ marginBottom: 16 }} />
      <Title level={4}>{record.login_mpt || id}</Title>
      <Row gutter={[16, 16]} style={{ marginBottom: 16 }}>
        <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="状态" valueRender={() => <Tag color={running ? 'green' : record.enabled ? 'blue' : 'default'}>{running ? '运行中' : record.enabled ? '已启用' : '已禁用'}</Tag>} /></Card></Col>
        <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="运行时长" value={relayState?.create_time ? formatOnlineTime(relayState.create_time) : '-'} /></Card></Col>
        <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="节点" value={relayState?.node_name || '-'} /></Card></Col>
        <Col span={6}><Card style={{ borderColor: '#2e3450' }}><Statistic title="连接状态" value={running ? '已连接' : '未连接'} /></Card></Col>
      </Row>
      <Card title="配置信息" style={{ borderColor: '#2e3450', marginBottom: 16 }}>
        <Descriptions column={2} size="small">
          <Descriptions.Item label="本地挂载点">{record.login_mpt}</Descriptions.Item>
          <Descriptions.Item label="目标挂载点">{record.target_mpt}</Descriptions.Item>
          <Descriptions.Item label="目标地址">{record.target_ip}:{record.target_port}</Descriptions.Item>
          <Descriptions.Item label="账号">{record.target_account || '-'}</Descriptions.Item>
          <Descriptions.Item label="UID">{record.uid}</Descriptions.Item>
          <Descriptions.Item label="启用状态">{record.enabled ? '启用' : '禁用'}</Descriptions.Item>
        </Descriptions>
      </Card>
      <Card title="运行状态" style={{ borderColor: '#2e3450', marginBottom: 16 }}>
        <Descriptions column={2} size="small">
          <Descriptions.Item label="节点 UID">{relayState?.node_uid || '-'}</Descriptions.Item>
          <Descriptions.Item label="Connect Key">{relayState?.connect_key || '-'}</Descriptions.Item>
          <Descriptions.Item label="创建时间">{relayState?.create_time ? formatOnlineTime(relayState.create_time) : '-'}</Descriptions.Item>
          <Descriptions.Item label="最近刷新">{relayState?.update_time ? formatOnlineTime(relayState.update_time) : '-'}</Descriptions.Item>
        </Descriptions>
      </Card>
      <Card title="操作" style={{ borderColor: '#2e3450' }}>
        <Space>
          <Button type="primary" onClick={handleToggle}>{running ? '停止' : '启动'}</Button>
          <Button onClick={() => navigate('/relay/pull')}>返回列表</Button>
          <Button disabled>编辑入口保留在列表页</Button>
        </Space>
      </Card>
    </div>
  );
};

export default PullRelayDetail;