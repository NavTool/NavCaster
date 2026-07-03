import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { AuthGuard } from './control/components/AuthGuard';
import { ControlShell } from './control/components/ControlShell';
import AdminAccessAccountsPage from './control/pages/AdminAccessAccountsPage';
import AdminUsersPage from './control/pages/AdminUsersPage';
import ConfigVersionsPage from './control/pages/ConfigVersionsPage';
import DashboardPage from './control/pages/DashboardPage';
import HostsPage from './control/pages/HostsPage';
import LoginPage from './control/pages/LoginPage';
import OperationsPage from './control/pages/OperationsPage';
import PlaceholderPage from './control/pages/PlaceholderPage';
import ProfilePage from './control/pages/ProfilePage';
import RegisterPage from './control/pages/RegisterPage';
import RuntimeDetailPage from './control/pages/RuntimeDetailPage';
import RuntimesPage from './control/pages/RuntimesPage';
import UsageRecordsPage from './control/pages/UsageRecordsPage';
import UserAccessAccountsPage from './control/pages/UserAccessAccountsPage';
import WorkersPage from './control/pages/WorkersPage';

const placeholder = (title: string, description: string) => (
  <PlaceholderPage title={title} description={description} />
);

export default function AppRouter() {
  return (
    <HashRouter>
      <Routes>
        <Route path="/login" element={<LoginPage />} />
        <Route path="/register" element={<RegisterPage />} />
        <Route path="/" element={<Navigate to="/admin/control/dashboard" replace />} />
        <Route path="/admin/control" element={<AuthGuard><ControlShell /></AuthGuard>}>
          <Route index element={<Navigate to="/admin/control/dashboard" replace />} />
          <Route path="dashboard" element={<DashboardPage />} />
          <Route path="operations" element={<OperationsPage />} />
          <Route path="nodes" element={<HostsPage />} />
          <Route path="users" element={<AdminUsersPage />} />
          <Route path="groups" element={placeholder('分组管理', '挂载点组')} />
          <Route path="mounts" element={<RuntimesPage />} />
          <Route path="access" element={<AdminAccessAccountsPage />} />
          <Route path="access-accounts" element={<AdminAccessAccountsPage />} />
          <Route path="relay" element={placeholder('接入管理', 'Relay Pull / Relay Push')} />
          <Route path="announcements" element={placeholder('公告', '设置展示给客户的公告')} />
          <Route path="server-nodes" element={<HostsPage />} />
          <Route path="redeem-codes" element={placeholder('兑换码', '生成兑换码')} />
          <Route path="coupons" element={placeholder('优惠码', '生成优惠码')} />
          <Route path="usage" element={<UsageRecordsPage />} />
          <Route path="settings" element={<ConfigVersionsPage />} />
          <Route path="user/access-accounts" element={<UserAccessAccountsPage />} />
          <Route path="user/usage" element={<UsageRecordsPage />} />
          <Route path="user/redeem" element={placeholder('兑换', '兑换兑换码或者优惠码')} />
          <Route path="user/profile" element={<ProfilePage />} />
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
