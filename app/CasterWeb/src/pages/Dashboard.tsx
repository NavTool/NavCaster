import React from 'react';
import { Card, Col, Row, Statistic, Tag, Typography, Spin, Alert, Descriptions, Progress } from 'antd';
import {
  CloudServerOutlined, UserOutlined, ClusterOutlined,
  ArrowUpOutlined, ArrowDownOutlined,
} from '@ant-design/icons';
import { useMultiSSE } from '../hooks/useSSE';
import type { CasterNode, ServerState, ClientState } from '../api/types';
import { formatBytes, formatMbps, formatOnlineTime, formatDelay, formatUsage, formatSpeed } from '../utils/format';

const { Title } = Typography;

const Dashboard: React.FC = () => {
  const { data: sseData, connected } = useMultiSSE<{
    nodes: Record<string, CasterNode>;
    servers: Record<string, ServerState>;
    clients: Record<string, ClientState>;
  }>(['nodes', 'servers', 'clients']);
  const nodesLoading = !connected && !sseData.nodes;
  const nodes = sseData.nodes ?? null;
  const servers = sseData.servers ?? null;
  const clients = sseData.clients ?? null;

  const nodeList = nodes ? Object.values(nodes) : [];
  const serverCount = servers ? Object.keys(servers).length : 0;
  const clientCount = clients ? Object.keys(clients).length : 0;

  // Aggregate from nodes — match QML CasterResourceController
  const totalSendSpeed = nodeList.reduce((s, n) => s + (n.send_speed || 0), 0);
  const totalRecvSpeed = nodeList.reduce((s, n) => s + (n.recv_speed || 0), 0);
  const avgCpu = nodeList.length > 0 ? nodeList.reduce((s, n) => s + (n.cpu_usage || 0), 0) / nodeList.length : 0;
  const totalMem = nodeList.reduce((s, n) => s + (n.mem_usage || 0), 0);
  const minOnlineTime = nodeList.length > 0 ? Math.min(...nodeList.map(n => n.online_time || 0)) : 0;

  if (nodesLoading) return <Spin size="large" style={{ display: 'block', margin: '100px auto' }} />;

  return (
    <div>
      <Title level={4}>集群概览</Title>
      <Row gutter={[16, 16]}>
        <Col xs={12} sm={8} lg={6}>
          <Card>
            <Statistic title="负载" value={formatUsage(avgCpu)} />
            <Progress percent={Math.round(avgCpu)} size="small" showInfo={false}
              strokeColor={avgCpu > 85 ? '#ff4d4f' : avgCpu > 50 ? '#faad14' : '#52c41a'} />
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card>
            <Statistic title="在线基站" value={serverCount} prefix={<CloudServerOutlined />} valueStyle={{ color: '#3f8600' }} />
            <Progress percent={Math.min(serverCount, 100)} size="small" showInfo={false} />
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card>
            <Statistic title="在线移动站" value={clientCount} prefix={<UserOutlined />} valueStyle={{ color: '#1677ff' }} />
            <Progress percent={Math.min(clientCount, 100)} size="small" showInfo={false} strokeColor="#1677ff" />
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card>
            <Statistic title="节点状态" value={`${nodeList.length} / ${nodeList.length}`} prefix={<ClusterOutlined />} />
            <Progress percent={100} size="small" showInfo={false} strokeColor="#52c41a" />
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card><Statistic title="内存占用" value={formatBytes(totalMem)} /></Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card><Statistic title="输入" value={formatMbps(totalRecvSpeed)} prefix={<ArrowDownOutlined />} valueStyle={{ color: '#cf1322' }} /></Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card><Statistic title="输出" value={formatMbps(totalSendSpeed)} prefix={<ArrowUpOutlined />} valueStyle={{ color: '#3f8600' }} /></Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card><Statistic title="运行时长" value={formatOnlineTime(minOnlineTime)} /></Card>
        </Col>
      </Row>

      <Title level={4} style={{ marginTop: 24 }}>节点状态</Title>
      <Row gutter={[16, 16]}>
        {nodeList.map((node: CasterNode) => (
          <Col xs={24} sm={12} lg={8} key={node.uid}>
            <Card
              title={`节点ID: ${node.node_name || node.uid}`}
              extra={<Tag color="green">在线</Tag>}
              size="small"
            >
              <Descriptions column={1} size="small" labelStyle={{ fontWeight: 'bold', width: 90 }}>
                <Descriptions.Item label="节点版本">{node.tag_version || node.set_version}</Descriptions.Item>
                <Descriptions.Item label="运行平台">{node.run_platform}</Descriptions.Item>
                <Descriptions.Item label="连接数量">
                  {node.connect_count} ({node.server_count} 基站 {node.client_count} 移动站)
                </Descriptions.Item>
                <Descriptions.Item label="节点负载">{(node.cpu_usage || 0).toFixed(2)}%</Descriptions.Item>
                <Descriptions.Item label="内存占用">{formatBytes(node.mem_usage || 0)}</Descriptions.Item>
                <Descriptions.Item label="处理延迟">{formatDelay(node.queue_delay || 0)}</Descriptions.Item>
                <Descriptions.Item label="网络延迟">
                  PUB {formatDelay(node.pub_tcp_delay || 0)} / SUB {formatDelay(node.sub_tcp_delay || 0)}
                </Descriptions.Item>
                <Descriptions.Item label="数据延迟">
                  PUB {formatDelay(node.pub_ping_delay || 0)} / SUB {formatDelay(node.sub_ping_delay || 0)}
                </Descriptions.Item>
                <Descriptions.Item label="输入流量">
                  {formatBytes(node.recv_total || 0)} ({formatSpeed(node.recv_speed || 0)})
                </Descriptions.Item>
                <Descriptions.Item label="输出流量">
                  {formatBytes(node.send_total || 0)} ({formatSpeed(node.send_speed || 0)})
                </Descriptions.Item>
                <Descriptions.Item label="运行时长">{formatOnlineTime(node.online_time || 0)}</Descriptions.Item>
              </Descriptions>
            </Card>
          </Col>
        ))}
        {nodeList.length === 0 && <Col span={24}><Alert message="暂无节点数据" type="info" /></Col>}
      </Row>
    </div>
  );
};

export default Dashboard;
