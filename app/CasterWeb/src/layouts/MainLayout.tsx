import React, { useState } from 'react';
import { Layout, Menu, Button, theme } from 'antd';
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
  { key: '/relay/pull', icon: <SwapOutlined />, label: '数据接入' },
  { key: '/relay/push', icon: <SwapOutlined />, label: '数据推送' },
];

const MainLayout: React.FC = () => {
  const [collapsed, setCollapsed] = useState(false);
  const navigate = useNavigate();
  const location = useLocation();
  const { token: { colorBgContainer, borderRadiusLG } } = theme.useToken();

  const handleLogout = async () => {
    await logout();
    navigate('/login');
  };

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider trigger={null} collapsible collapsed={collapsed} theme="dark">
        <div style={{
          height: 48, margin: 12, display: 'flex', alignItems: 'center',
          justifyContent: 'center', color: '#fff', fontWeight: 'bold', fontSize: collapsed ? 14 : 18,
          whiteSpace: 'nowrap', overflow: 'hidden',
        }}>
          {collapsed ? 'NC' : 'NavCaster'}
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[location.pathname]}
          items={menuItems}
          onClick={({ key }) => navigate(key)}
        />
      </Sider>
      <Layout>
        <Header style={{ padding: '0 16px', background: colorBgContainer, display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <Button
            type="text"
            icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
            onClick={() => setCollapsed(!collapsed)}
          />
          <Button type="text" icon={<LogoutOutlined />} onClick={handleLogout}>
            退出
          </Button>
        </Header>
        <Content style={{ margin: 16, padding: 20, background: colorBgContainer, borderRadius: borderRadiusLG, overflow: 'auto' }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  );
};

export default MainLayout;
