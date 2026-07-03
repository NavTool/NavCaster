import {
  ApiOutlined,
  BellOutlined,
  ClusterOutlined,
  CloudServerOutlined,
  ControlOutlined,
  DashboardOutlined,
  DatabaseOutlined,
  DownOutlined,
  GiftOutlined,
  GlobalOutlined,
  HistoryOutlined,
  IdcardOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  NodeIndexOutlined,
  NotificationOutlined,
  ProfileOutlined,
  SettingOutlined,
  TagsOutlined,
  TeamOutlined,
  UserOutlined,
  WalletOutlined,
} from '@ant-design/icons';
import { Badge, Button, Dropdown, Tooltip } from 'antd';
import { NavLink, Outlet, useLocation, useNavigate } from 'react-router-dom';
import { type ReactNode, useEffect, useMemo, useState } from 'react';
import { getSession, type AuthSessionSubject } from '../../api/session';
import { setStoredToken } from '../../api/client';

type NavItem = {
  path: string;
  label: string;
  icon: ReactNode;
};

const navSections: Array<{ title: string; items: NavItem[] }> = [
  {
    title: '管理页面',
    items: [
      { path: '/admin/control/dashboard', label: '仪表盘', icon: <DashboardOutlined /> },
      { path: '/admin/control/operations', label: '运维监控', icon: <DatabaseOutlined /> },
      { path: '/admin/control/nodes', label: '节点管理', icon: <NodeIndexOutlined /> },
      { path: '/admin/control/users', label: '用户管理', icon: <UserOutlined /> },
      { path: '/admin/control/groups', label: '分组管理', icon: <TeamOutlined /> },
      { path: '/admin/control/mounts', label: '挂载点管理', icon: <ClusterOutlined /> },
      { path: '/admin/control/access', label: '接入管理', icon: <ApiOutlined /> },
      { path: '/admin/control/announcements', label: '公告', icon: <NotificationOutlined /> },
      { path: '/admin/control/server-nodes', label: '节点管理', icon: <CloudServerOutlined /> },
      { path: '/admin/control/redeem-codes', label: '兑换码', icon: <GiftOutlined /> },
      { path: '/admin/control/coupons', label: '优惠码', icon: <TagsOutlined /> },
      { path: '/admin/control/usage', label: '使用记录', icon: <HistoryOutlined /> },
      { path: '/admin/control/settings', label: '系统设置', icon: <SettingOutlined /> },
    ],
  },
  {
    title: '用户视图/供应商视图',
    items: [
      { path: '/admin/control/user/access-accounts', label: '接入账号管理', icon: <IdcardOutlined /> },
      { path: '/admin/control/user/usage', label: '使用记录', icon: <HistoryOutlined /> },
      { path: '/admin/control/user/redeem', label: '兑换', icon: <GiftOutlined /> },
      { path: '/admin/control/user/profile', label: '个人资料', icon: <ProfileOutlined /> },
    ],
  },
];

const pageTitles: Record<string, string> = {
  '/admin/control/dashboard': '管理控制台',
  '/admin/control/operations': '运维监控',
  '/admin/control/nodes': '节点管理',
  '/admin/control/users': '用户管理',
  '/admin/control/groups': '分组管理',
  '/admin/control/mounts': '挂载点管理',
  '/admin/control/access': '接入管理',
  '/admin/control/announcements': '公告',
  '/admin/control/server-nodes': '节点管理',
  '/admin/control/redeem-codes': '兑换码',
  '/admin/control/coupons': '优惠码',
  '/admin/control/usage': '使用记录',
  '/admin/control/settings': '系统设置',
  '/admin/control/user/access-accounts': '接入账号管理',
  '/admin/control/user/usage': '使用记录',
  '/admin/control/user/redeem': '兑换',
  '/admin/control/user/profile': '个人资料',
  '/admin/control/hosts': '节点管理',
  '/admin/control/runtimes': '挂载点管理',
  '/admin/control/workers': '运维监控',
  '/admin/control/config': '系统设置',
};

const pageSubtitles: Record<string, string> = {
  '/admin/control/dashboard': '系统概览与统计数据',
  '/admin/control/operations': 'NTRIP Caster 节点、挂载点与数据链路监控',
  '/admin/control/usage': '查看每个用户、每个接入账号的挂载点用量记录',
  '/admin/control/user/usage': '查看当前账户下每个接入账号的用量记录',
};

export function ControlShell() {
  const [collapsed, setCollapsed] = useState(false);
  const [session, setSession] = useState<AuthSessionSubject | null>(null);
  const [language, setLanguage] = useState(() => localStorage.getItem('navcasterLanguage') || 'CN ZH');
  const location = useLocation();
  const navigate = useNavigate();
  const pageTitle = useMemo(() => {
    const matched = Object.keys(pageTitles).find((path) => location.pathname.startsWith(path));
    return matched ? pageTitles[matched] : 'Control plane';
  }, [location.pathname]);
  const pageSubtitle = useMemo(() => {
    const matched = Object.keys(pageSubtitles).find((path) => location.pathname.startsWith(path));
    return matched ? pageSubtitles[matched] : 'AdminService';
  }, [location.pathname]);
  const userName = session?.username || session?.account_id || 'KORO';
  const userInitials = userName.slice(0, 2).toUpperCase();
  const userRole = session?.role || 'Admin';

  const setLanguagePreference = (next: string) => {
    setLanguage(next);
    localStorage.setItem('navcasterLanguage', next);
  };

  const logout = () => {
    setStoredToken('');
    navigate('/admin/control/dashboard');
  };

  useEffect(() => {
    let cancelled = false;
    getSession()
      .then((subject) => {
        if (!cancelled) setSession(subject);
      })
      .catch(() => {
        if (!cancelled) setSession(null);
      });
    return () => { cancelled = true; };
  }, []);

  return (
    <div className={`control-shell ${collapsed ? 'control-shell-collapsed' : ''}`}>
      <aside className="control-sidebar">
        <div className="control-brand">
          <div className="control-brand-mark">
            <ControlOutlined />
          </div>
          <div className="control-brand-copy">
            <strong>NavCaster</strong>
            <span>Control Plane</span>
          </div>
        </div>
        <nav className="control-nav" aria-label="control plane navigation">
          {navSections.map((section) => (
            <div className="control-nav-section" key={section.title}>
              <div className="control-nav-section-title">{section.title}</div>
              {section.items.map((item) => (
                <Tooltip
                  key={item.path}
                  title={collapsed ? item.label : ''}
                  placement="right"
                >
                  <NavLink to={item.path} className={({ isActive }) => `control-nav-link ${isActive ? 'active' : ''}`}>
                    {item.icon}
                    <span className="control-nav-label">{item.label}</span>
                  </NavLink>
                </Tooltip>
              ))}
            </div>
          ))}
        </nav>
        <div className="control-sidebar-footer">
          <Tooltip title={collapsed ? 'Expand sidebar' : 'Collapse sidebar'} placement="right">
            <Button
              type="text"
              icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
              onClick={() => setCollapsed((value) => !value)}
            />
          </Tooltip>
        </div>
      </aside>

      <div className="control-main">
        <header className="control-topbar">
          <div>
            <div className="control-topbar-kicker">
              {pageSubtitle}
            </div>
            <h2>{pageTitle}</h2>
          </div>
          <div className="control-topbar-right">
            <Tooltip title="公告与通知">
              <Button
                className="control-icon-button"
                type="text"
                icon={<Badge dot><BellOutlined /></Badge>}
                onClick={() => navigate('/admin/control/announcements')}
              />
            </Tooltip>

            <Dropdown
              trigger={['click']}
              menu={{
                selectedKeys: [language],
                items: [
                  { key: 'CN ZH', label: 'CN ZH' },
                  { key: 'EN', label: 'EN' },
                ],
                onClick: ({ key }) => setLanguagePreference(String(key)),
              }}
            >
              <Button className="control-topbar-button" type="text">
                <GlobalOutlined /> {language} <DownOutlined />
              </Button>
            </Dropdown>

            <Dropdown
              trigger={['click']}
              menu={{
                items: [
                  { key: 'usage', label: '使用记录', icon: <HistoryOutlined /> },
                  { key: 'redeem', label: '兑换', icon: <GiftOutlined /> },
                ],
                onClick: ({ key }) => {
                  if (key === 'usage') navigate('/admin/control/user/usage');
                  if (key === 'redeem') navigate('/admin/control/user/redeem');
                },
              }}
            >
              <Button className="control-balance-pill" type="text">
                <WalletOutlined /> $5800.23
              </Button>
            </Dropdown>

            <Dropdown
              trigger={['click']}
              menu={{
                items: [
                  { key: 'profile', label: '个人资料', icon: <ProfileOutlined /> },
                  { key: 'settings', label: '系统设置', icon: <SettingOutlined /> },
                  { type: 'divider' },
                  { key: 'logout', label: '退出登录', icon: <LogoutOutlined /> },
                ],
                onClick: ({ key }) => {
                  if (key === 'profile') navigate('/admin/control/user/profile');
                  if (key === 'settings') navigate('/admin/control/settings');
                  if (key === 'logout') logout();
                },
              }}
            >
              <Button className="control-account-menu" type="text">
                <span className="control-avatar">{userInitials}</span>
                <span className="control-account-copy">
                  <strong>{userName}</strong>
                  <small>{userRole}</small>
                </span>
                <DownOutlined />
              </Button>
            </Dropdown>
          </div>
        </header>
        <main className="control-content">
          <Outlet />
        </main>
      </div>
    </div>
  );
}
