import { Navigate } from 'react-router-dom';
import type { ReactNode } from 'react';
import { getStoredToken } from '../../api/client';

export function AuthGuard({ children }: { children: ReactNode }) {
  return getStoredToken() ? children : <Navigate to="/login" replace />;
}
