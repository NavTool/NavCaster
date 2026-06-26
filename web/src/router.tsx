import { HashRouter, Routes, Route, Navigate } from 'react-router-dom';
import MainLayout from './layouts/MainLayout';
import Login from './pages/Login';
import Dashboard from './pages/Dashboard';
import Servers from './pages/Servers';
import Clients from './pages/Clients';
import Accounts from './pages/Accounts';
import Sources from './pages/Sources';
import Aliases from './pages/Aliases';
import AccessGroups from './pages/AccessGroups';
import AccessGroupDetail from './pages/AccessGroupDetail';
import PullRelay from './pages/PullRelay';
import PushRelay from './pages/PushRelay';
import Settings from './pages/Settings';
import NodeDetail from './pages/NodeDetail';
import ServerDetail from './pages/ServerDetail';
import ClientDetail from './pages/ClientDetail';
import AccountDetail from './pages/AccountDetail';
import SourceTable from './pages/SourceTable';
import ConnectionHistory from './pages/ConnectionHistory';
import ConnectionHistoryDetail from './pages/ConnectionHistoryDetail';
import Statistics from './pages/Statistics';
import SystemMonitor from './pages/SystemMonitor';
import AuditLog from './pages/AuditLog';
import RingLog from './pages/RingLog';
import OperationsDashboard from './pages/OperationsDashboard';
import SelfServiceWorkspace from './pages/SelfServiceWorkspace';
import { getToken, getBaseURL, setToken, setAuthUser } from './api/client';
import { getSession } from './api/operations';
import { isRoleAllowed, RoleSessionProvider, roleHome } from './role';
import type { AccountRole, AuthSessionSubject } from './api/types';
import { useEffect, useState } from 'react';
import { Spin, Result, Button } from 'antd';
import ErrorBoundary from './components/ErrorBoundary';

type GuardState = 'checking' | 'ok' | 'unauth' | 'unreachable';

function RequireAuth({ children }: { children: (session: AuthSessionSubject) => React.ReactNode }) {
  const [state, setState] = useState<GuardState>('checking');
  const [session, setSession] = useState<AuthSessionSubject | null>(null);

  useEffect(() => {
    if (!getToken() || !getBaseURL()) {
      setState('unauth');
      return;
    }
    let cancelled = false;
    getSession()
      .then((subject) => {
        if (cancelled) return;
        setSession(subject);
        setState('ok');
      })
      .catch((error) => {
        if (cancelled) return;
        if (!getToken() || error?.response?.status === 401 || error?.response?.status === 403) {
          setState('unauth');
        } else {
          setState('unreachable');
        }
      });
    return () => { cancelled = true; };
  }, []);

  if (state === 'unauth') {
    return <Navigate to="/login" replace />;
  }
  if (state === 'checking') {
    return <Spin size="large" style={{ display: 'block', margin: '200px auto' }} />;
  }
  if (state === 'unreachable') {
    return (
      <Result
        status="warning"
        title="无法连接到 NavCaster 后端"
        subTitle="请检查服务器是否在线或网络连通性，然后重新登录。"
        extra={
          <Button
            type="primary"
            onClick={() => {
              setToken(null);
              setAuthUser(null);
              window.location.hash = '#/login';
            }}
          >
            返回登录页
          </Button>
        }
        style={{ marginTop: 80 }}
      />
    );
  }
  if (!session) {
    return <Spin size="large" style={{ display: 'block', margin: '200px auto' }} />;
  }
  return <RoleSessionProvider session={session}><ErrorBoundary>{children(session)}</ErrorBoundary></RoleSessionProvider>;
}

function RoleRoute({ allowed }: { allowed: AccountRole[] }) {
  return (
    <RequireAuth>
      {(session) => (
        isRoleAllowed(session.role, allowed)
          ? <MainLayout />
          : (
            <Result
              status="403"
              title="无权访问"
              subTitle="当前账号角色不能进入该运营区域。"
              extra={<Button type="primary" href={`#${roleHome(session)}`}>返回角色首页</Button>}
              style={{ marginTop: 80 }}
            />
          )
      )}
    </RequireAuth>
  );
}

export default function AppRouter() {
  return (
    <HashRouter>
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route
          path="/"
          element={
            <RequireAuth>
              {(session) => <Navigate to={roleHome(session)} replace />}
            </RequireAuth>
          }
        />
        <Route
          path="/admin"
          element={<RoleRoute allowed={['admin']} />}
        >
          <Route index element={<Navigate to="/admin/dashboard" replace />} />
          <Route path="dashboard" element={<OperationsDashboard scope="admin" />} />
          <Route path="accounts" element={<OperationsDashboard scope="admin" view="accounts" />} />
          <Route path="access-accounts" element={<OperationsDashboard scope="admin" view="access-accounts" />} />
          <Route path="mount-point-groups" element={<OperationsDashboard scope="admin" view="mount-point-groups" />} />
          <Route path="mount-points" element={<OperationsDashboard scope="admin" view="mount-points" />} />
          <Route path="stations" element={<OperationsDashboard scope="admin" view="stations" />} />
          <Route path="usage" element={<OperationsDashboard scope="admin" view="usage" />} />
          <Route path="data-push-usage" element={<OperationsDashboard scope="admin" view="data-push-usage" />} />
          <Route path="supply-usage" element={<OperationsDashboard scope="admin" view="supply-usage" />} />
          <Route path="legacy">
            <Route index element={<Navigate to="/admin/legacy/dashboard" replace />} />
            <Route path="dashboard" element={<Dashboard />} />
            <Route path="nodes/:id" element={<NodeDetail />} />
            <Route path="servers" element={<Servers />} />
            <Route path="servers/:id" element={<ServerDetail />} />
            <Route path="clients" element={<Clients />} />
            <Route path="clients/:id" element={<ClientDetail />} />
            <Route path="accounts" element={<Accounts />} />
            <Route path="accounts/:id" element={<AccountDetail />} />
            <Route path="sources" element={<Sources />} />
            <Route path="aliases" element={<Aliases />} />
            <Route path="access" element={<AccessGroups />} />
            <Route path="access/:id" element={<AccessGroupDetail />} />
            <Route path="sourcetable" element={<SourceTable />} />
            <Route path="relay/pull" element={<PullRelay />} />
            <Route path="relay/push" element={<PushRelay />} />
            <Route path="history" element={<ConnectionHistory />} />
            <Route path="history/:type/:name" element={<ConnectionHistoryDetail />} />
            <Route path="statistics" element={<Statistics />} />
            <Route path="monitor" element={<SystemMonitor />} />
            <Route path="audit" element={<AuditLog />} />
            <Route path="logs/ring" element={<RingLog />} />
            <Route path="settings" element={<Settings />} />
          </Route>
        </Route>
        <Route
          path="/me"
          element={<RoleRoute allowed={['admin', 'user']} />}
        >
          <Route index element={<Navigate to="/me/dashboard" replace />} />
          <Route path="dashboard" element={<SelfServiceWorkspace scope="me" view="dashboard" />} />
          <Route path="profile" element={<SelfServiceWorkspace scope="me" view="profile" />} />
          <Route path="access-accounts" element={<SelfServiceWorkspace scope="me" view="access-accounts" />} />
          <Route path="groups" element={<SelfServiceWorkspace scope="me" view="groups" />} />
          <Route path="mount-points" element={<SelfServiceWorkspace scope="me" view="mount-points" />} />
          <Route path="usage" element={<SelfServiceWorkspace scope="me" view="usage" />} />
          <Route path="data-push" element={<SelfServiceWorkspace scope="me" view="data-push" />} />
        </Route>
        <Route
          path="/supplier"
          element={<RoleRoute allowed={['admin', 'supplier']} />}
        >
          <Route index element={<Navigate to="/supplier/dashboard" replace />} />
          <Route path="dashboard" element={<SelfServiceWorkspace scope="supplier" view="dashboard" />} />
          <Route path="profile" element={<SelfServiceWorkspace scope="supplier" view="profile" />} />
          <Route path="access-accounts" element={<SelfServiceWorkspace scope="supplier" view="access-accounts" />} />
          <Route path="stations" element={<SelfServiceWorkspace scope="supplier" view="stations" />} />
          <Route path="supply-usage" element={<SelfServiceWorkspace scope="supplier" view="supply-usage" />} />
          <Route path="earnings" element={<SelfServiceWorkspace scope="supplier" view="earnings" />} />
        </Route>
        <Route
          path="/"
          element={<RoleRoute allowed={['admin']} />}
        >
          <Route index element={<Navigate to="/admin/dashboard" replace />} />
          <Route path="dashboard" element={<Dashboard />} />
          <Route path="nodes/:id" element={<NodeDetail />} />
          <Route path="servers" element={<Servers />} />
          <Route path="servers/:id" element={<ServerDetail />} />
          <Route path="clients" element={<Clients />} />
          <Route path="clients/:id" element={<ClientDetail />} />
          <Route path="accounts" element={<Accounts />} />
          <Route path="accounts/:id" element={<AccountDetail />} />
          <Route path="sources" element={<Sources />} />
          <Route path="aliases" element={<Aliases />} />
          <Route path="access" element={<AccessGroups />} />
          <Route path="access/:id" element={<AccessGroupDetail />} />
          <Route path="sourcetable" element={<SourceTable />} />
          <Route path="relay/pull" element={<PullRelay />} />
          <Route path="relay/push" element={<PushRelay />} />
          <Route path="history" element={<ConnectionHistory />} />
          <Route path="history/:type/:name" element={<ConnectionHistoryDetail />} />
          <Route path="statistics" element={<Statistics />} />
          <Route path="monitor" element={<SystemMonitor />} />
          <Route path="audit" element={<AuditLog />} />
          <Route path="logs/ring" element={<RingLog />} />
          <Route path="settings" element={<Settings />} />
        </Route>
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </HashRouter>
  );
}
