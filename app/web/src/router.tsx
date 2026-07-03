import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { ControlShell } from './control/components/ControlShell';
import ConfigVersionsPage from './control/pages/ConfigVersionsPage';
import DashboardPage from './control/pages/DashboardPage';
import HostsPage from './control/pages/HostsPage';
import OperationsPage from './control/pages/OperationsPage';
import PlaceholderPage from './control/pages/PlaceholderPage';
import RuntimeDetailPage from './control/pages/RuntimeDetailPage';
import RuntimesPage from './control/pages/RuntimesPage';
import UsageRecordsPage from './control/pages/UsageRecordsPage';
import WorkersPage from './control/pages/WorkersPage';

const placeholder = (title: string, description: string) => (
  <PlaceholderPage title={title} description={description} />
);

export default function AppRouter() {
  return (
    <HashRouter>
      <Routes>
        <Route path="/" element={<Navigate to="/admin/control/dashboard" replace />} />
        <Route path="/admin/control" element={<ControlShell />}>
          <Route index element={<Navigate to="/admin/control/dashboard" replace />} />
          <Route path="dashboard" element={<DashboardPage />} />
          <Route path="operations" element={<OperationsPage />} />
          <Route path="nodes" element={<HostsPage />} />
          <Route path="users" element={placeholder('用户管理', '注册和管理所有账号，管理员、用户、供应商')} />
          <Route path="groups" element={placeholder('分组管理', '挂载点组')} />
          <Route path="mounts" element={<RuntimesPage />} />
          <Route path="access" element={placeholder('接入管理', 'Relay Pull / Relay Push')} />
          <Route path="announcements" element={placeholder('公告', '设置展示给客户的公告')} />
          <Route path="server-nodes" element={<HostsPage />} />
          <Route path="redeem-codes" element={placeholder('兑换码', '生成兑换码')} />
          <Route path="coupons" element={placeholder('优惠码', '生成优惠码')} />
          <Route path="usage" element={<UsageRecordsPage />} />
          <Route path="settings" element={<ConfigVersionsPage />} />
          <Route path="user/access-accounts" element={placeholder('接入账号管理', '管理用户自己的接入账号')} />
          <Route path="user/usage" element={<UsageRecordsPage />} />
          <Route path="user/redeem" element={placeholder('兑换', '兑换兑换码或者优惠码')} />
          <Route path="user/profile" element={placeholder('个人资料', '管理自己的用户资料')} />
          <Route path="hosts" element={<HostsPage />} />
          <Route path="runtimes" element={<RuntimesPage />} />
          <Route path="runtimes/:id" element={<RuntimeDetailPage />} />
          <Route path="workers" element={<WorkersPage />} />
          <Route path="config" element={<ConfigVersionsPage />} />
        </Route>
        <Route path="*" element={<Navigate to="/admin/control/dashboard" replace />} />
      </Routes>
    </HashRouter>
  );
}
