import React, { useState } from 'react';
import { Layout, Menu, Button } from 'antd';
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
} from '@ant-design/icons';
import { Outlet, useNavigate, useLocation } from 'react-router-dom';
import { logout } from '../api/auth';

const { Header, Sider, Content } = Layout;

const menuItems = [
  { key: '/dashboard', icon: <DashboardOutlined />, label: '节点状态' },
  { key: '/servers', icon: <CloudServerOutlined />, label: '基准站' },
  { key: '/clients', icon: <UserOutlined />, label: '移动站' },
  { key: '/accounts', icon: <TeamOutlined />, label: '账号管理' },
  { key: '/sources', icon: <DatabaseOutlined />, label: '源列表' },
  { key: '/aliases', icon: <BranchesOutlined />, label: '挂载点别名' },
  { key: '/access', icon: <LockOutlined />, label: '访问管理' },
  { key: '/sourcetable', icon: <TableOutlined />, label: '源表视图' },
  { key: '/relay/pull', icon: <SwapOutlined />, label: '数据接入' },
  { key: '/relay/push', icon: <SwapOutlined />, label: '数据推送' },
  { key: '/history', icon: <HistoryOutlined />, label: '连接历史' },
  { key: '/statistics', icon: <BarChartOutlined />, label: '数据统计' },
  { key: '/settings', icon: <SettingOutlined />, label: '系统设置' },
];

const MainLayout: React.FC = () => {
  const [collapsed, setCollapsed] = useState(false);
  const navigate = useNavigate();
  const location = useLocation();

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
          <Button
            type="text"
            icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
            onClick={() => setCollapsed(!collapsed)}
            style={{ color: '#8b90a8' }}
          />
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
