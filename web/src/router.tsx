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
import Statistics from './pages/Statistics';
import SystemMonitor from './pages/SystemMonitor';
import { getToken, getBaseURL } from './api/client';
import { getHealthCheck } from './api';
import { useEffect, useState } from 'react';
import { Spin } from 'antd';

function RequireAuth({ children }: { children: React.ReactNode }) {
  const [checking, setChecking] = useState(true);

  useEffect(() => {
    if (!getToken() || !getBaseURL()) {
      setChecking(false);
      return;
    }
    getHealthCheck()
      .then(() => setChecking(false))
      .catch(() => setChecking(false));
  }, []);

  if (!getToken() || !getBaseURL()) {
    return <Navigate to="/login" replace />;
  }
  if (checking) {
    return <Spin size="large" style={{ display: 'block', margin: '200px auto' }} />;
  }
  return <>{children}</>;
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
          <Route path="statistics" element={<Statistics />} />
          <Route path="monitor" element={<SystemMonitor />} />
          <Route path="settings" element={<Settings />} />
        </Route>
      </Routes>
    </HashRouter>
  );
}
