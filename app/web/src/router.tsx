import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { AuthGuard } from './control/components/AuthGuard';
import { ControlShell } from './control/components/ControlShell';
import AccessAccountDetailPage from './control/pages/AccessAccountDetailPage';
import AccessMonitorDetailPage from './control/pages/AccessMonitorDetailPage';
import AccessMonitorPage from './control/pages/AccessMonitorPage';
import AdminAccessAccountsPage from './control/pages/AdminAccessAccountsPage';
import AdminUsersPage from './control/pages/AdminUsersPage';
import CasterNodesPage from './control/pages/CasterNodesPage';
import ConfigVersionsPage from './control/pages/ConfigVersionsPage';
import DashboardPage from './control/pages/DashboardPage';
import HostsPage from './control/pages/HostsPage';
import InstanceDetailPage from './control/pages/InstanceDetailPage';
import InstancesPage from './control/pages/InstancesPage';
import LoginPage from './control/pages/LoginPage';
import MountManagementPage from './control/pages/MountManagementPage';
import OperationsPage from './control/pages/OperationsPage';
import PlaceholderPage from './control/pages/PlaceholderPage';
import ProfilePage from './control/pages/ProfilePage';
import RegisterPage from './control/pages/RegisterPage';
import RuntimeDetailPage from './control/pages/RuntimeDetailPage';
import RuntimesPage from './control/pages/RuntimesPage';
import UsageRecordsPage from './control/pages/UsageRecordsPage';
import UserAccessAccountsPage from './control/pages/UserAccessAccountsPage';
import WorkersPage from './control/pages/WorkersPage';

const placeholder = () => (
  <PlaceholderPage />
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
          <Route path="instances" element={<InstancesPage />} />
          <Route path="instances/:id" element={<InstanceDetailPage />} />
          <Route path="nodes" element={<CasterNodesPage />} />
          <Route path="nodes/:id" element={<RuntimeDetailPage />} />
          <Route path="access-monitor" element={<Navigate to="/admin/control/access-monitor/base-stations" replace />} />
          <Route path="access-monitor/base-stations" element={<AccessMonitorPage mode="base" />} />
          <Route path="access-monitor/base-stations/:id" element={<AccessMonitorDetailPage mode="base" />} />
          <Route path="access-monitor/mobile-stations" element={<AccessMonitorPage mode="mobile" />} />
          <Route path="access-monitor/mobile-stations/:id" element={<AccessMonitorDetailPage mode="mobile" />} />
          <Route path="users" element={<AdminUsersPage />} />
          <Route path="groups" element={placeholder()} />
          <Route path="mounts" element={<MountManagementPage />} />
          <Route path="access" element={<AdminAccessAccountsPage />} />
          <Route path="access/:id" element={<AccessAccountDetailPage />} />
          <Route path="access-accounts" element={<AdminAccessAccountsPage />} />
          <Route path="access-accounts/:id" element={<AccessAccountDetailPage />} />
          <Route path="relay" element={placeholder()} />
          <Route path="announcements" element={placeholder()} />
          <Route path="server-nodes" element={<InstancesPage />} />
          <Route path="redeem-codes" element={placeholder()} />
          <Route path="coupons" element={placeholder()} />
          <Route path="usage" element={<UsageRecordsPage />} />
          <Route path="settings" element={<ConfigVersionsPage />} />
          <Route path="user/access-accounts" element={<UserAccessAccountsPage />} />
          <Route path="user/access-accounts/:id" element={<AccessAccountDetailPage scope="user" />} />
          <Route path="user/usage" element={<UsageRecordsPage />} />
          <Route path="user/redeem" element={placeholder()} />
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
