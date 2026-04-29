import React, { useState } from 'react';
import { Layout, Menu, Button, Space, Tag } from 'antd';
import {
  DashboardOutlined,
  CloudServerOutlined,
  UserOutlined,
  TeamOutlined,
  DatabaseOutlined,
  SwapOutlined,
  BranchesOutlined,
  LockOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  SettingOutlined,
  TableOutlined,
  HistoryOutlined,
  BarChartOutlined,
  ClusterOutlined,
  ApiOutlined,
} from '@ant-design/icons';
import { Outlet, useNavigate, useLocation } from 'react-router-dom';
import { logout } from '../api/auth';
import { useMultiSSE } from '../hooks/useSSE';
import type { CasterNode } from '../api/types';

const { Header, Sider, Content } = Layout;

const menuItems = [
  {
    key: 'monitor',
    icon: <DashboardOutlined />,
    label: '监控',
    children: [
      { key: '/dashboard', icon: <ClusterOutlined />, label: '集群总览' },
      { key: '/statistics', icon: <BarChartOutlined />, label: '数据统计' },
      { key: '/monitor', icon: <DashboardOutlined />, label: '系统监控' },
    ],
  },
  {
    key: 'connection',
    icon: <ApiOutlined />,
    label: '连接',
    children: [
      { key: '/servers', icon: <CloudServerOutlined />, label: '基准站' },
      { key: '/clients', icon: <UserOutlined />, label: '移动站' },
    ],
  },
  {
    key: 'user',
    icon: <TeamOutlined />,
    label: '用户',
    children: [
      { key: '/accounts', icon: <TeamOutlined />, label: '账号管理' },
      { key: '/access', icon: <LockOutlined />, label: '访问管理' },
    ],
  },
  {
    key: 'mountpoint',
    icon: <DatabaseOutlined />,
    label: '挂载点',
    children: [
      { key: '/sources', icon: <DatabaseOutlined />, label: '源列表' },
      { key: '/aliases', icon: <BranchesOutlined />, label: '挂载点别名' },
      { key: '/sourcetable', icon: <TableOutlined />, label: '源表视图' },
    ],
  },
  {
    key: 'relay',
    icon: <SwapOutlined />,
    label: '数据转发',
    children: [
      { key: '/relay/pull', icon: <SwapOutlined />, label: '数据接入' },
      { key: '/relay/push', icon: <SwapOutlined />, label: '数据推送' },
    ],
  },
  {
    key: 'logs',
    icon: <HistoryOutlined />,
    label: '日志',
    children: [
      { key: '/history', icon: <HistoryOutlined />, label: '连接历史' },
      { key: '/audit', icon: <HistoryOutlined />, label: '审计日志' },
      { key: '/logs/ring', icon: <HistoryOutlined />, label: '进程日志' },
    ],
  },
  {
    key: 'system',
    icon: <SettingOutlined />,
    label: '系统',
    children: [
      { key: '/settings', icon: <SettingOutlined />, label: '系统设置' },
    ],
  },
];

const MainLayout: React.FC = () => {
  const [collapsed, setCollapsed] = useState(false);
  const navigate = useNavigate();
  const location = useLocation();

  // 集群状态摘要 (Header 显示)
  const { data: sseData, connected: sseConnected } = useMultiSSE<{
    nodes: Record<string, CasterNode>;
    servers: Record<string, unknown>;
    clients: Record<string, unknown>;
  }>(['nodes', 'servers', 'clients']);

  const nodes = sseData.nodes || {};
  const nodeCount = Object.keys(nodes).length;
  const masterNode = Object.values(nodes).find(n => n.master);
  const serverCount = Object.keys(sseData.servers || {}).length;
  const clientCount = Object.keys(sseData.clients || {}).length;

  // 根据当前路径确定展开的菜单组
  const openKeys = menuItems
    .filter(group => group.children?.some(c => location.pathname.startsWith(c.key)))
    .map(group => group.key);

  const handleLogout = async () => {
    try { await logout(); } catch { /* ignore */ }
    navigate('/login');
  };

  return (
    <Layout style={{ minHeight: '100vh', background: '#141625' }}>
      <Sider trigger={null} collapsible collapsed={collapsed} width={220}
        style={{ background: '#12142a', borderRight: '1px solid #1e2245' }}>
        <div style={{
          height: 56, margin: '0 16px', display: 'flex', alignItems: 'center',
          justifyContent: collapsed ? 'center' : 'flex-start', gap: 10,
          borderBottom: '1px solid #1e2245',
        }}>
          <div style={{
            width: 32, height: 32, borderRadius: 8, background: '#4a8eff',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            color: '#fff', fontWeight: 700, fontSize: 14, flexShrink: 0,
          }}>NC</div>
          {!collapsed && (
            <span style={{ color: '#e8eaf0', fontWeight: 600, fontSize: 16, letterSpacing: -0.3 }}>
              NavCaster
            </span>
          )}
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[location.pathname]}
          defaultOpenKeys={openKeys}
          items={menuItems}
          onClick={({ key }) => navigate(key)}
          style={{ background: 'transparent', borderRight: 'none', marginTop: 8 }}
        />
      </Sider>
      <Layout style={{ background: '#141625' }}>
        <Header style={{
          padding: '0 20px', background: '#1a1e34', display: 'flex',
          alignItems: 'center', justifyContent: 'space-between',
          borderBottom: '1px solid #1e2245', height: 56,
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <Button
              type="text"
              icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
              onClick={() => setCollapsed(!collapsed)}
              style={{ color: '#8b90a8' }}
            />
            <Space size={16} style={{ marginLeft: 8 }}>
              <span style={{ color: '#8b90a8', fontSize: 13 }}>
                节点 <Tag color={nodeCount > 0 ? 'green' : 'default'}>{nodeCount}</Tag>
              </span>
              <span style={{ color: '#8b90a8', fontSize: 13 }}>
                基站 <Tag color="blue">{serverCount}</Tag>
              </span>
              <span style={{ color: '#8b90a8', fontSize: 13 }}>
                用户 <Tag color="cyan">{clientCount}</Tag>
              </span>
              {masterNode && (
                <span style={{ color: '#8b90a8', fontSize: 13 }}>
                  Master <Tag color="gold">{masterNode.node_name}</Tag>
                </span>
              )}
              <span style={{ color: '#8b90a8', fontSize: 13, display: 'inline-flex', alignItems: 'center', gap: 4 }}
                title={sseConnected ? 'SSE 实时连接正常' : 'SSE 未连接，数据不会自动刷新'}>
                <span style={{
                  display: 'inline-block', width: 8, height: 8, borderRadius: '50%',
                  background: sseConnected ? '#52c41a' : '#ff4d4f',
                  boxShadow: sseConnected ? '0 0 6px #52c41a' : 'none',
                }} />
                SSE
              </span>
            </Space>
          </div>
          <Button type="text" icon={<LogoutOutlined />} onClick={handleLogout}
            style={{ color: '#8b90a8' }}>
            退出
          </Button>
        </Header>
        <Content style={{
          margin: 16, padding: 24, background: '#1a1e34',
          borderRadius: 12, overflow: 'auto', border: '1px solid #1e2245',
        }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  );
};

export default MainLayout;
