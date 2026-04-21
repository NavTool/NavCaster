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
import { getToken, getBaseURL, probeBackend, setToken, setAuthUser } from './api/client';
import { useEffect, useState } from 'react';
import { Spin, Result, Button } from 'antd';
import ErrorBoundary from './components/ErrorBoundary';

type GuardState = 'checking' | 'ok' | 'unauth' | 'unreachable';

function RequireAuth({ children }: { children: React.ReactNode }) {
  const [state, setState] = useState<GuardState>('checking');

  useEffect(() => {
    if (!getToken() || !getBaseURL()) {
      setState('unauth');
      return;
    }
    let cancelled = false;
    probeBackend().then((ok) => {
      if (cancelled) return;
      if (ok) {
        setState('ok');
      } else if (!getToken()) {
        // probeBackend cleared token via 401/403 interceptor
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
  return <ErrorBoundary>{children}</ErrorBoundary>;
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
              <MainLayout />
            </RequireAuth>
          }
        >
          <Route index element={<Navigate to="/dashboard" replace />} />
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
      </Routes>
    </HashRouter>
  );
}
