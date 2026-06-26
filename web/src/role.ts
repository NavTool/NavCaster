import React, { createContext, useContext } from 'react';
import type { AccountRole, AuthSessionSubject } from './api/types';

export type RoleScope = AccountRole;

const RoleSessionContext = createContext<AuthSessionSubject | null>(null);

export function RoleSessionProvider({ session, children }: { session: AuthSessionSubject; children: React.ReactNode }) {
  return React.createElement(RoleSessionContext.Provider, { value: session }, children);
}

export function useRoleSession(): AuthSessionSubject {
  const session = useContext(RoleSessionContext);
  if (!session) {
    throw new Error('Role session is not available');
  }
  return session;
}

export function roleHome(session: Pick<AuthSessionSubject, 'role'>): string {
  if (session.role === 'admin') return '/admin/dashboard';
  if (session.role === 'supplier') return '/supplier/dashboard';
  return '/me/dashboard';
}

export function isRoleAllowed(role: string, allowed: AccountRole[]): boolean {
  return allowed.includes(role as AccountRole);
}
