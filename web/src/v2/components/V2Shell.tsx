import {
  AppstoreOutlined,
  ClusterOutlined,
  CloudServerOutlined,
  CodeOutlined,
  ControlOutlined,
  DatabaseOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  NodeIndexOutlined,
} from '@ant-design/icons';
import { Button, Tooltip } from 'antd';
import { NavLink, Outlet, useLocation } from 'react-router-dom';
import { useMemo, useState } from 'react';
import type { AuthSessionSubject } from '../../api/types';
import { V2_ADMIN_SERVICE_MODE } from '../api/adminService';

const navItems = [
  { path: '/admin/control/hosts', label: 'Hosts', icon: <CloudServerOutlined /> },
  { path: '/admin/control/runtimes', label: 'Runtimes', icon: <ClusterOutlined /> },
  { path: '/admin/control/workers', label: 'Workers', icon: <NodeIndexOutlined /> },
  { path: '/admin/control/config', label: 'Config', icon: <DatabaseOutlined /> },
];

const pageTitles: Record<string, string> = {
  '/admin/control/hosts': 'Host fleet',
  '/admin/control/runtimes': 'Runtime desired state',
  '/admin/control/workers': 'Worker metrics',
  '/admin/control/config': 'Config versions',
};

export function V2Shell({ session }: { session: AuthSessionSubject }) {
  const [collapsed, setCollapsed] = useState(false);
  const location = useLocation();
  const pageTitle = useMemo(() => {
    const matched = Object.keys(pageTitles).find((path) => location.pathname.startsWith(path));
    return matched ? pageTitles[matched] : 'Control plane';
  }, [location.pathname]);

  return (
    <div className={`v2-shell ${collapsed ? 'v2-shell-collapsed' : ''}`}>
      <aside className="v2-sidebar">
        <div className="v2-brand">
          <div className="v2-brand-mark">
            <ControlOutlined />
          </div>
          <div className="v2-brand-copy">
            <strong>NavCaster v2</strong>
            <span>Control Plane</span>
          </div>
        </div>
        <nav className="v2-nav" aria-label="v2 control plane navigation">
          {navItems.map((item) => (
            <Tooltip key={item.path} title={collapsed ? item.label : ''} placement="right">
              <NavLink to={item.path} className={({ isActive }) => `v2-nav-link ${isActive ? 'active' : ''}`}>
                {item.icon}
                <span>{item.label}</span>
              </NavLink>
            </Tooltip>
          ))}
        </nav>
        <div className="v2-sidebar-footer">
          <Tooltip title={collapsed ? 'Expand sidebar' : 'Collapse sidebar'} placement="right">
            <Button
              type="text"
              icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />}
              onClick={() => setCollapsed((value) => !value)}
            />
          </Tooltip>
        </div>
      </aside>

      <div className="v2-main">
        <header className="v2-topbar">
          <div>
            <div className="v2-topbar-kicker">
              <AppstoreOutlined /> v2 AdminService
            </div>
            <h2>{pageTitle}</h2>
          </div>
          <div className="v2-topbar-right">
            <span className="v2-contract-pill">
              <CodeOutlined /> {V2_ADMIN_SERVICE_MODE === 'mock' ? 'mock contract' : 'live AdminService'}
            </span>
            <div className="v2-user-chip">
              <span>{session.username || session.account_id}</span>
              <small>{session.role}</small>
            </div>
          </div>
        </header>
        <main className="v2-content">
          <Outlet />
        </main>
      </div>
    </div>
  );
}
