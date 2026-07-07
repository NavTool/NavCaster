import { getSession as getIdentitySession, type SessionSubject } from './identity';

export type AccountRole = 'admin' | 'user' | string;

export type AuthSessionSubject = SessionSubject & { compat_admin?: boolean; token?: string };

export async function getSession(): Promise<AuthSessionSubject> {
  return getIdentitySession();
}
