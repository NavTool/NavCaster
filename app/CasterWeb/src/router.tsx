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
import { getToken } from './api/client';

function RequireAuth({ children }: { children: React.ReactNode }) {
  if (!getToken()) {
    return <Navigate to="/login" replace />;
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
          <Route path="servers" element={<Servers />} />
          <Route path="clients" element={<Clients />} />
          <Route path="accounts" element={<Accounts />} />
          <Route path="sources" element={<Sources />} />
          <Route path="aliases" element={<Aliases />} />
          <Route path="access" element={<AccessGroups />} />
          <Route path="relay/pull" element={<PullRelay />} />
          <Route path="relay/push" element={<PushRelay />} />
        </Route>
      </Routes>
    </HashRouter>
  );
}
