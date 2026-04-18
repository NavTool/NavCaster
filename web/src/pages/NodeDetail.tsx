import React from 'react';
import { Typography, Card, Breadcrumb, Row, Col, Statistic, Empty } from 'antd';
import { useParams, Link } from 'react-router-dom';
import { useMultiSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { CasterNode } from '../api/types';
import { formatBytes, formatOnlineTime, formatDelay, formatSpeed } from '../utils/format';

const { Title } = Typography;

const NodeDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const { data: sseData } = useMultiSSE<{ nodes: Record<string, CasterNode> }>(['nodes']);
  const node = sseData.nodes?.[id || ''];

  return (
    <div>
      <Breadcrumb items={[
        { title: <Link to="/dashboard">节点状态</Link> },
        { title: node?.node_name || id || '节点详情' },
      ]} style={{ marginBottom: 16 }} />
      <Title level={4}>{node?.node_name || id}</Title>

      {node ? (
        <>
          <Row gutter={[16, 16]} style={{ marginBottom: 24 }}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="状态" valueRender={() => <StatusIndicator status="online" pulse label="在线" />} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="CPU" value={`${(node.cpu_usage || 0).toFixed(1)}%`} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="内存" value={formatBytes(node.mem_usage || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="运行时长" value={formatOnlineTime(node.online_time || 0)} />
              </Card>
            </Col>
          </Row>
          <Row gutter={[16, 16]}>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="基站数" value={node.server_count} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="移动站数" value={node.client_count} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="输入" value={formatSpeed(node.recv_speed || 0)} />
              </Card>
            </Col>
            <Col span={6}>
              <Card style={{ borderColor: '#2e3450' }}>
                <Statistic title="输出" value={formatSpeed(node.send_speed || 0)} />
              </Card>
            </Col>
          </Row>
          <Card title="延迟信息" style={{ marginTop: 16, borderColor: '#2e3450' }}>
            <Row gutter={16}>
              <Col span={8}><Statistic title="处理延迟" value={formatDelay(node.queue_delay || 0)} /></Col>
              <Col span={8}><Statistic title="PUB TCP" value={formatDelay(node.pub_tcp_delay || 0)} /></Col>
              <Col span={8}><Statistic title="SUB TCP" value={formatDelay(node.sub_tcp_delay || 0)} /></Col>
            </Row>
          </Card>
          <Card title="历史记录" style={{ marginTop: 16, borderColor: '#2e3450' }}>
            <Empty description="时序数据功能开发中" />
          </Card>
        </>
      ) : (
        <Card style={{ borderColor: '#2e3450' }}>
          <Empty description={`未找到节点 ${id}`} />
        </Card>
      )}
    </div>
  );
};

export default NodeDetail;
