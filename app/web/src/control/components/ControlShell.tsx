import {
  ApiOutlined,
  BellOutlined,
  ClusterOutlined,
  CloudServerOutlined,
  ControlOutlined,
  DashboardOutlined,
  DatabaseOutlined,
  DownOutlined,
  EnvironmentOutlined,
  GiftOutlined,
  GlobalOutlined,
  HistoryOutlined,
  IdcardOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  MobileOutlined,
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
import { getStoredToken, setStoredToken } from '../../api/client';
import { logout as logoutSession } from '../../api/identity';
import { accountRoleLabel } from '../labels';

type NavItem = {
  path: string;
  label: string;
  icon: ReactNode;
  children?: NavItem[];
};

const navSections: Array<{ title: string; items: NavItem[] }> = [
  {
    title: '管理页面',
    items: [
      { path: '/admin/control/dashboard', label: '仪表盘', icon: <DashboardOutlined /> },
      { path: '/admin/control/operations', label: '运维监控', icon: <DatabaseOutlined /> },
      { path: '/admin/control/instances', label: '实例管理', icon: <CloudServerOutlined /> },
      { path: '/admin/control/nodes', label: '节点管理', icon: <NodeIndexOutlined /> },
      {
        path: '/admin/control/access-monitor',
        label: '接入监控',
        icon: <ApiOutlined />,
        children: [
          { path: '/admin/control/access-monitor/base-stations', label: '基准站', icon: <EnvironmentOutlined /> },
          { path: '/admin/control/access-monitor/mobile-stations', label: '移动站', icon: <MobileOutlined /> },
        ],
      },
      { path: '/admin/control/users', label: '用户管理', icon: <UserOutlined /> },
      { path: '/admin/control/access', label: '账号管理', icon: <IdcardOutlined /> },
      { path: '/admin/control/groups', label: '分组管理', icon: <TeamOutlined /> },
      { path: '/admin/control/mounts', label: '挂载点管理', icon: <ClusterOutlined /> },
      { path: '/admin/control/relay', label: '接入管理', icon: <ApiOutlined /> },
      { path: '/admin/control/announcements', label: '公告', icon: <NotificationOutlined /> },
      { path: '/admin/control/redeem-codes', label: '兑换码', icon: <GiftOutlined /> },
      { path: '/admin/control/coupons', label: '优惠码', icon: <TagsOutlined /> },
      { path: '/admin/control/usage', label: '使用记录', icon: <HistoryOutlined /> },
      { path: '/admin/control/settings', label: '系统设置', icon: <SettingOutlined /> },
    ],
  },
  {
    title: '用户视图',
    items: [
      { path: '/admin/control/user/access-accounts', label: '账号管理', icon: <IdcardOutlined /> },
      { path: '/admin/control/user/usage', label: '使用记录', icon: <HistoryOutlined /> },
      { path: '/admin/control/user/redeem', label: '兑换', icon: <GiftOutlined /> },
      { path: '/admin/control/user/profile', label: '个人资料', icon: <ProfileOutlined /> },
    ],
  },
];

const pageTitles: Record<string, string> = {
  '/admin/control/dashboard': '管理控制台',
  '/admin/control/operations': '运维监控',
  '/admin/control/instances': '实例管理',
  '/admin/control/nodes': '节点管理',
  '/admin/control/access-monitor/base-stations': '基准站接入监控',
  '/admin/control/access-monitor/mobile-stations': '移动站接入监控',
  '/admin/control/access-monitor': '接入监控',
  '/admin/control/users': '用户管理',
  '/admin/control/access': '账号管理',
  '/admin/control/groups': '分组管理',
  '/admin/control/mounts': '挂载点管理',
  '/admin/control/relay': '接入管理',
  '/admin/control/announcements': '公告',
  '/admin/control/server-nodes': '实例管理',
  '/admin/control/redeem-codes': '兑换码',
  '/admin/control/coupons': '优惠码',
  '/admin/control/usage': '使用记录',
  '/admin/control/settings': '系统设置',
  '/admin/control/user/access-accounts': '账号管理',
  '/admin/control/user/usage': '使用记录',
  '/admin/control/user/redeem': '兑换',
  '/admin/control/user/profile': '个人资料',
  '/admin/control/hosts': '实例管理',
  '/admin/control/runtimes': '挂载点管理',
  '/admin/control/workers': '运维监控',
  '/admin/control/config': '系统设置',
};

const pageSubtitles: Record<string, string> = {
  '/admin/control/dashboard': '系统概览与统计数据',
  '/admin/control/operations': 'NTRIP Caster 节点、挂载点与数据链路监控',
  '/admin/control/instances': 'Agent 实例、设备资源和实例下 Caster 控制',
  '/admin/control/nodes': '当前运行的 Caster 节点和控制意图',
  '/admin/control/access-monitor/base-stations': '实时在线基准站和历史在线记录',
  '/admin/control/access-monitor/mobile-stations': '实时在线移动站和历史使用记录',
  '/admin/control/users': 'Web 用户账号和登录权限',
  '/admin/control/access': '全局搜索接入账号并查看使用详情',
  '/admin/control/usage': '查看每个用户、每个接入账号的挂载点用量记录',
  '/admin/control/user/access-accounts': '管理当前账号名下的设备接入凭证',
  '/admin/control/user/usage': '查看当前账户下每个接入账号的用量记录',
};

export function ControlShell() {
  const [collapsed, setCollapsed] = useState(false);
  const [session, setSession] = useState<AuthSessionSubject | null>(null);
  const [language, setLanguage] = useState(() => {
    const stored = localStorage.getItem('navcasterLanguage');
    return stored && !['CN ZH', 'EN'].includes(stored) ? stored : '简体中文';
  });
  const location = useLocation();
  const navigate = useNavigate();
  const pageTitle = useMemo(() => {
    const matched = Object.keys(pageTitles).sort((left, right) => right.length - left.length).find((path) => location.pathname.startsWith(path));
    return matched ? pageTitles[matched] : '控制面';
  }, [location.pathname]);
  const pageSubtitle = useMemo(() => {
    const matched = Object.keys(pageSubtitles).sort((left, right) => right.length - left.length).find((path) => location.pathname.startsWith(path));
    return matched ? pageSubtitles[matched] : 'AdminService 控制服务';
  }, [location.pathname]);
  const visibleSections = useMemo(() => {
    if (session?.role === 'admin') return navSections;
    return navSections.map((section) => ({
      ...section,
      items: section.title === '管理页面'
        ? section.items.filter((item) => item.path === '/admin/control/dashboard')
        : section.items,
    }));
  }, [session?.role]);
  const userName = session?.username || session?.account_id || '未登录';
  const userInitials = userName.slice(0, 2).toUpperCase();
  const userRole = accountRoleLabel(session?.role || 'user');

  const setLanguagePreference = (next: string) => {
    setLanguage(next);
    localStorage.setItem('navcasterLanguage', next);
  };

  const logout = async () => {
    await logoutSession();
    navigate('/login', { replace: true });
  };

  const isPathActive = (path: string) => location.pathname === path || location.pathname.startsWith(`${path}/`);

  useEffect(() => {
    let cancelled = false;
    if (!getStoredToken()) {
      navigate('/login', { replace: true });
      return () => { cancelled = true; };
    }
    getSession()
      .then((subject) => {
        if (!cancelled) setSession(subject);
      })
      .catch(() => {
        if (!cancelled) {
          setStoredToken('');
          setSession(null);
          navigate('/login', { replace: true });
        }
      });
    return () => { cancelled = true; };
  }, [navigate]);

  return (
    <div className={`control-shell ${collapsed ? 'control-shell-collapsed' : ''}`}>
      <aside className="control-sidebar">
        <div className="control-brand">
          <div className="control-brand-mark">
            <ControlOutlined />
          </div>
          <div className="control-brand-copy">
            <strong>NavCaster</strong>
            <span>控制面</span>
          </div>
        </div>
        <nav className="control-nav" aria-label="控制面导航">
          {visibleSections.map((section) => (
            <div className="control-nav-section" key={section.title}>
              <div className="control-nav-section-title">{section.title}</div>
              {section.items.map((item) => {
                const itemActive = isPathActive(item.path);
                if (item.children?.length) {
                  return (
                    <div className={`control-nav-group ${itemActive ? 'active' : ''}`} key={item.path}>
                      <Tooltip title={collapsed ? item.label : ''} placement="right">
                        <NavLink to={item.children[0].path} className={`control-nav-link control-nav-parent ${itemActive ? 'active' : ''}`}>
                          {item.icon}
                          <span className="control-nav-label">{item.label}</span>
                        </NavLink>
                      </Tooltip>
                      {!collapsed ? (
                        <div className="control-nav-children">
                          {item.children.map((child) => (
                            <NavLink key={child.path} to={child.path} className={({ isActive }) => `control-nav-link control-nav-child ${isActive ? 'active' : ''}`}>
                              {child.icon}
                              <span className="control-nav-label">{child.label}</span>
                            </NavLink>
                          ))}
                        </div>
                      ) : null}
                    </div>
                  );
                }
                return (
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
                );
              })}
            </div>
          ))}
        </nav>
        <div className="control-sidebar-footer">
          <Tooltip title={collapsed ? '展开侧边栏' : '收起侧边栏'} placement="right">
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
                  { key: '简体中文', label: '简体中文' },
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
