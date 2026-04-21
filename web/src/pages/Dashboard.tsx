import React, { useEffect, useState } from 'react';
import { Card, Col, Row, Typography, Spin, Alert, Progress, Tag } from 'antd';
import {
  CloudServerOutlined, UserOutlined, ClusterOutlined,
  ArrowUpOutlined, ArrowDownOutlined, DashboardOutlined,
  HddOutlined, ClockCircleOutlined, CrownOutlined,
} from '@ant-design/icons';
import { useMultiSSE } from '../hooks/useSSE';
import StatusIndicator from '../components/StatusIndicator';
import type { CasterNode, ServerState, ClientState } from '../api/types';
import { formatBytes, formatMbps, formatOnlineTime, formatDelay, formatUsage, formatSpeed } from '../utils/format';
import { useNavigate } from 'react-router-dom';
import { getSystemStatus } from '../api';

const { Title, Text } = Typography;

const Dashboard: React.FC = () => {
  const navigate = useNavigate();
  const [masterNode, setMasterNode] = useState<string | null>(null);
  const { data: sseData, connected } = useMultiSSE<{
    nodes: Record<string, CasterNode>;
    servers: Record<string, ServerState>;
    clients: Record<string, ClientState>;
  }>(['nodes', 'servers', 'clients']);

  useEffect(() => {
    const fetchMaster = async () => {
      try {
        const status = await getSystemStatus();
        setMasterNode(status?.master_node ?? null);
      } catch { /* ignore */ }
    };
    fetchMaster();
    const interval = setInterval(fetchMaster, 10000);
    return () => clearInterval(interval);
  }, []);
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

  const cpuColor = avgCpu > 85 ? '#ff4d4f' : avgCpu > 50 ? '#faad14' : '#52c41a';

  return (
    <div>
      <div style={{ marginBottom: 20 }}>
        <Title level={4} style={{ margin: 0 }}>NtripCaster Dashboard</Title>
        <Text style={{ color: '#6b7194', fontSize: 13 }}>监控和管理您的 GNSS 差分数据基础设施</Text>
      </div>

      {/* Spark-style metric cards */}
      <Row gutter={[16, 16]}>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>负载</div>
                <div style={{ fontSize: 28, fontWeight: 700, letterSpacing: -0.5 }}>{formatUsage(avgCpu)}</div>
              </div>
              <DashboardOutlined style={{ fontSize: 28, color: '#4a8eff', opacity: 0.5 }} />
            </div>
            <Progress percent={Math.round(avgCpu)} size="small" showInfo={false} strokeColor={cpuColor}
              style={{ marginTop: 8 }} />
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>在线基站</div>
                <div style={{ fontSize: 28, fontWeight: 700 }}>{serverCount}</div>
              </div>
              <CloudServerOutlined style={{ fontSize: 28, color: '#52c41a', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>在线移动站</div>
                <div style={{ fontSize: 28, fontWeight: 700 }}>{clientCount}</div>
              </div>
              <UserOutlined style={{ fontSize: 28, color: '#4a8eff', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>节点状态</div>
                <div style={{ fontSize: 28, fontWeight: 700 }}>{nodeList.length}/{nodeList.length}</div>
              </div>
              <ClusterOutlined style={{ fontSize: 28, color: '#52c41a', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>内存占用</div>
                <div style={{ fontSize: 28, fontWeight: 700 }}>{formatBytes(totalMem)}</div>
              </div>
              <HddOutlined style={{ fontSize: 28, color: '#8b90a8', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>输入</div>
                <div style={{ fontSize: 28, fontWeight: 700, color: '#ff7875' }}>{formatMbps(totalRecvSpeed)}</div>
              </div>
              <ArrowDownOutlined style={{ fontSize: 28, color: '#ff7875', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>输出</div>
                <div style={{ fontSize: 28, fontWeight: 700, color: '#52c41a' }}>{formatMbps(totalSendSpeed)}</div>
              </div>
              <ArrowUpOutlined style={{ fontSize: 28, color: '#52c41a', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
        <Col xs={12} sm={8} lg={6}>
          <Card className="metric-card" style={{ borderColor: '#2e3450' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
              <div>
                <div style={{ fontSize: 13, color: '#8b90a8', marginBottom: 4 }}>运行时长</div>
                <div style={{ fontSize: 28, fontWeight: 700 }}>{formatOnlineTime(minOnlineTime)}</div>
              </div>
              <ClockCircleOutlined style={{ fontSize: 28, color: '#8b90a8', opacity: 0.5 }} />
            </div>
          </Card>
        </Col>
      </Row>

      {/* Spark-style node cards */}
      <Title level={4} style={{ marginTop: 24 }}>节点状态</Title>
      <Row gutter={[16, 16]}>
        {nodeList.map((node: CasterNode) => (
          <Col xs={24} sm={12} md={8} lg={6} key={node.uid}>
            <Card className="node-card" style={{ borderColor: '#2e3450', cursor: 'pointer' }}
              onClick={() => navigate(`/nodes/${encodeURIComponent(node.uid)}`)}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                  <ClusterOutlined style={{ fontSize: 22, color: '#4a8eff' }} />
                  <div>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                      <span style={{ fontSize: 16, fontWeight: 600 }}>{node.node_name || node.uid}</span>
                      {masterNode === node.uid && (
                        <Tag color="gold" icon={<CrownOutlined />} style={{ margin: 0, fontSize: 11, lineHeight: '18px', padding: '0 5px' }}>Master</Tag>
                      )}
                    </div>
                    <div style={{ fontSize: 12, color: '#6b7194', fontFamily: 'monospace' }}>
                      v{node.tag_version || node.set_version}
                    </div>
                  </div>
                </div>
                <StatusIndicator status="online" pulse size="md" />
              </div>

              {/* Quick stats row like Spark's NodesView */}
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, marginBottom: 14 }}>
                <div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: 12, color: '#6b7194', marginBottom: 2 }}>
                    <CloudServerOutlined style={{ fontSize: 12 }} />
                    <span>基站</span>
                  </div>
                  <div style={{ fontSize: 22, fontWeight: 700 }}>{node.server_count}</div>
                </div>
                <div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: 12, color: '#6b7194', marginBottom: 2 }}>
                    <UserOutlined style={{ fontSize: 12 }} />
                    <span>移动站</span>
                  </div>
                  <div style={{ fontSize: 22, fontWeight: 700 }}>{node.client_count}</div>
                </div>
              </div>

              {/* CPU bar */}
              <div style={{ marginBottom: 10 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, marginBottom: 4 }}>
                  <span style={{ color: '#6b7194' }}>CPU 负载</span>
                  <span style={{ fontWeight: 500 }}>{(node.cpu_usage || 0).toFixed(1)}%</span>
                </div>
                <Progress percent={Math.round(node.cpu_usage || 0)} size="small" showInfo={false}
                  strokeColor={(node.cpu_usage || 0) > 85 ? '#ff4d4f' : (node.cpu_usage || 0) > 50 ? '#faad14' : '#52c41a'} />
              </div>

              {/* Memory bar */}
              <div style={{ marginBottom: 14 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, marginBottom: 4 }}>
                  <span style={{ color: '#6b7194' }}>内存</span>
                  <span style={{ fontWeight: 500 }}>{formatBytes(node.mem_usage || 0)}</span>
                </div>
              </div>

              {/* Details */}
              <div style={{ borderTop: '1px solid #2e3450', paddingTop: 12, display: 'grid', gap: 6, fontSize: 12 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: '#6b7194' }}>平台</span>
                  <span>{node.run_platform}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: '#6b7194' }}>处理延迟</span>
                  <span>{formatDelay(node.queue_delay || 0)}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: '#6b7194' }}>网络延迟</span>
                  <span>PUB {formatDelay(node.pub_tcp_delay || 0)} / SUB {formatDelay(node.sub_tcp_delay || 0)}</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: '#6b7194' }}>输入</span>
                  <span>{formatBytes(node.recv_total || 0)} ({formatSpeed(node.recv_speed || 0)})</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: '#6b7194' }}>输出</span>
                  <span>{formatBytes(node.send_total || 0)} ({formatSpeed(node.send_speed || 0)})</span>
                </div>
                <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                  <span style={{ color: '#6b7194' }}>运行时长</span>
                  <span style={{ fontWeight: 500 }}>{formatOnlineTime(node.online_time || 0)}</span>
                </div>
              </div>
            </Card>
          </Col>
        ))}
        {nodeList.length === 0 && <Col span={24}><Alert message="暂无节点数据" type="info" /></Col>}
      </Row>
    </div>
  );
};

export default Dashboard;
