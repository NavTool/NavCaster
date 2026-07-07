package postgres

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/lib/pq"

	"navcaster-admin/internal/errorsx"
	"navcaster-admin/internal/identity"
)

type IdentityRepository struct {
	db *sql.DB
}

func NewIdentityRepository(db *sql.DB) *IdentityRepository {
	return &IdentityRepository{db: db}
}

func (r *IdentityRepository) CreateAccount(ctx context.Context, create identity.AccountCreate) (identity.Account, error) {
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.Account{}, err
	}
	defer rollback(tx)

	result, err := tx.ExecContext(ctx, `
INSERT INTO account_name_locks (username_norm, locked_reason)
VALUES ($1, 'registered')
ON CONFLICT (username_norm) DO NOTHING`, create.UsernameNorm)
	if err != nil {
		return identity.Account{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.Account{}, err
	}
	if rows == 0 {
		return identity.Account{}, errorsx.New(409, "username_reserved", "username is already reserved")
	}

	accountID := identity.NewID("acc")
	paramsJSON, err := json.Marshal(emptyMap(create.PasswordParams))
	if err != nil {
		return identity.Account{}, errorsx.BadRequest("password params must be JSON serializable")
	}
	_, err = tx.ExecContext(ctx, `
INSERT INTO accounts
  (account_id, username, username_norm, display_name, email, phone, role, status,
   password_hash, password_algo, password_params, created_via, created_by, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::jsonb, $12, $13, now(), now())`,
		accountID, create.Username, create.UsernameNorm, create.DisplayName, nullableString(create.Email),
		nullableString(create.Phone), string(create.Role), string(create.Status), create.PasswordHash,
		create.PasswordAlgo, string(paramsJSON), string(create.CreatedVia), nullableString(create.CreatedBy))
	if err != nil {
		if isUniqueViolation(err) {
			return identity.Account{}, errorsx.New(409, "username_reserved", "username is already reserved")
		}
		return identity.Account{}, err
	}
	if _, err := tx.ExecContext(ctx, `UPDATE account_name_locks SET account_id = $2 WHERE username_norm = $1`, create.UsernameNorm, accountID); err != nil {
		return identity.Account{}, err
	}
	if err := tx.Commit(); err != nil {
		return identity.Account{}, err
	}
	return r.GetAccount(ctx, accountID)
}

func (r *IdentityRepository) GetAccount(ctx context.Context, accountID string) (identity.Account, error) {
	row := r.db.QueryRowContext(ctx, accountSelectSQL()+` WHERE account_id = $1`, accountID)
	account, err := scanIdentityAccount(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.Account{}, errorsx.NotFound("account not found")
		}
		return identity.Account{}, err
	}
	return account, nil
}

func (r *IdentityRepository) GetAccountByUsernameNorm(ctx context.Context, usernameNorm string) (identity.Account, error) {
	row := r.db.QueryRowContext(ctx, accountSelectSQL()+` WHERE username_norm = $1`, usernameNorm)
	account, err := scanIdentityAccount(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.Account{}, errorsx.NotFound("account not found")
		}
		return identity.Account{}, err
	}
	return account, nil
}

func (r *IdentityRepository) GetAccountDetail(ctx context.Context, accountID string) (identity.AccountDetail, error) {
	row := r.db.QueryRowContext(ctx, `
SELECT a.account_id, a.username, a.username_norm, a.display_name, COALESCE(a.email, ''), COALESCE(a.phone, ''),
       a.role, a.status, a.password_hash, a.password_algo, a.password_params::text, a.created_via,
       COALESCE(a.created_by, ''), a.created_at, a.updated_at, a.disabled_at, a.deleted_at,
       (SELECT count(*) FROM access_accounts aa WHERE aa.owner_account_id = a.account_id AND aa.status <> 'deleted'),
       (SELECT count(*) FROM web_sessions ws WHERE ws.account_id = a.account_id AND ws.status = 'active' AND ws.expires_at > now())
FROM accounts a
WHERE a.account_id = $1`, accountID)
	account, accessCount, sessionCount, err := scanIdentityAccountDetail(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.AccountDetail{}, errorsx.NotFound("account not found")
		}
		return identity.AccountDetail{}, err
	}
	return identity.AccountDetail{Account: account, AccessAccountCount: accessCount, ActiveSessionCount: sessionCount}, nil
}

func (r *IdentityRepository) ListAccounts(ctx context.Context, filter identity.AccountListFilter) ([]identity.Account, int, error) {
	where, args := accountFilterSQL(filter)
	countQuery := `SELECT count(*) FROM accounts` + where
	var total int
	if err := r.db.QueryRowContext(ctx, countQuery, args...).Scan(&total); err != nil {
		return nil, 0, err
	}
	args = append(args, filter.Limit, filter.Offset)
	query := accountSelectSQL() + where + fmt.Sprintf(` ORDER BY created_at DESC, account_id DESC LIMIT $%d OFFSET $%d`, len(args)-1, len(args))
	rows, err := r.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, 0, err
	}
	defer rows.Close()
	var accounts []identity.Account
	for rows.Next() {
		account, err := scanIdentityAccount(rows)
		if err != nil {
			return nil, 0, err
		}
		accounts = append(accounts, account)
	}
	return accounts, total, rows.Err()
}

func (r *IdentityRepository) UpdateAccountProfile(ctx context.Context, accountID string, update identity.AccountProfileUpdate) (identity.Account, error) {
	result, err := r.db.ExecContext(ctx, `
UPDATE accounts
SET display_name = $2, email = $3, phone = $4, updated_at = now()
WHERE account_id = $1 AND status <> 'deleted'`,
		accountID, update.DisplayName, nullableString(update.Email), nullableString(update.Phone))
	if err != nil {
		return identity.Account{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.Account{}, err
	}
	if rows == 0 {
		return identity.Account{}, errorsx.NotFound("account not found")
	}
	return r.GetAccount(ctx, accountID)
}

func (r *IdentityRepository) SetAccountStatus(ctx context.Context, accountID string, status identity.AccountStatus) (identity.Account, []identity.AccessAccount, error) {
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.Account{}, nil, err
	}
	defer rollback(tx)
	account, err := getIdentityAccountTx(ctx, tx, accountID)
	if err != nil {
		return identity.Account{}, nil, err
	}
	if account.Status == identity.AccountStatusDeleted {
		return identity.Account{}, nil, errorsx.New(409, "invalid_state_transition", "deleted account cannot change status")
	}
	switch status {
	case identity.AccountStatusActive, identity.AccountStatusDisabled, identity.AccountStatusDeleted:
	default:
		return identity.Account{}, nil, errorsx.BadRequest("unsupported account status")
	}
	_, err = tx.ExecContext(ctx, `
UPDATE accounts
SET status = $2,
    disabled_at = CASE WHEN $2 = 'disabled' THEN now() ELSE disabled_at END,
    deleted_at = CASE WHEN $2 = 'deleted' THEN now() ELSE deleted_at END,
    updated_at = now()
WHERE account_id = $1`, accountID, string(status))
	if err != nil {
		return identity.Account{}, nil, err
	}
	if status == identity.AccountStatusDeleted {
		if _, err := tx.ExecContext(ctx, `UPDATE account_name_locks SET locked_reason = 'deleted' WHERE username_norm = $1`, account.UsernameNorm); err != nil {
			return identity.Account{}, nil, err
		}
		if _, err := tx.ExecContext(ctx, `
UPDATE access_accounts
SET status = 'disabled',
    disabled_at = COALESCE(disabled_at, now()),
    projection_version = nextval('auth_projection_version_seq'),
    updated_at = now()
WHERE owner_account_id = $1 AND status <> 'deleted'`, accountID); err != nil {
			return identity.Account{}, nil, err
		}
	}
	if status == identity.AccountStatusDisabled || status == identity.AccountStatusDeleted {
		if _, err := tx.ExecContext(ctx, `
UPDATE web_sessions
SET status = 'revoked', revoked_at = COALESCE(revoked_at, now())
WHERE account_id = $1 AND status = 'active'`, accountID); err != nil {
			return identity.Account{}, nil, err
		}
	}
	affected, err := listAccessAccountsTx(ctx, tx, `WHERE aa.owner_account_id = $1 AND aa.status <> 'deleted'`, accountID)
	if err != nil {
		return identity.Account{}, nil, err
	}
	if err := tx.Commit(); err != nil {
		return identity.Account{}, nil, err
	}
	updated, err := r.GetAccount(ctx, accountID)
	if err != nil {
		return identity.Account{}, nil, err
	}
	return updated, affected, nil
}

func (r *IdentityRepository) SetAccountPassword(ctx context.Context, accountID string, passwordHash string, passwordAlgo string, passwordParams map[string]any, revokeSessions bool) (identity.Account, error) {
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.Account{}, err
	}
	defer rollback(tx)
	paramsJSON, err := json.Marshal(emptyMap(passwordParams))
	if err != nil {
		return identity.Account{}, errorsx.BadRequest("password params must be JSON serializable")
	}
	result, err := tx.ExecContext(ctx, `
UPDATE accounts
SET password_hash = $2, password_algo = $3, password_params = $4::jsonb, updated_at = now()
WHERE account_id = $1 AND status <> 'deleted'`, accountID, passwordHash, passwordAlgo, string(paramsJSON))
	if err != nil {
		return identity.Account{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.Account{}, err
	}
	if rows == 0 {
		return identity.Account{}, errorsx.NotFound("account not found")
	}
	if revokeSessions {
		if _, err := tx.ExecContext(ctx, `
UPDATE web_sessions
SET status = 'revoked', revoked_at = COALESCE(revoked_at, now())
WHERE account_id = $1 AND status = 'active'`, accountID); err != nil {
			return identity.Account{}, err
		}
	}
	if err := tx.Commit(); err != nil {
		return identity.Account{}, err
	}
	return r.GetAccount(ctx, accountID)
}

func (r *IdentityRepository) RevokeSessionsForAccount(ctx context.Context, accountID string) (int, error) {
	result, err := r.db.ExecContext(ctx, `
UPDATE web_sessions
SET status = 'revoked', revoked_at = COALESCE(revoked_at, now())
WHERE account_id = $1 AND status = 'active'`, accountID)
	if err != nil {
		return 0, err
	}
	rows, err := result.RowsAffected()
	return int(rows), err
}

func (r *IdentityRepository) CreateSession(ctx context.Context, session identity.Session) (identity.Session, error) {
	_, err := r.db.ExecContext(ctx, `
INSERT INTO web_sessions
  (session_id, account_id, token_hash, status, issued_at, expires_at, user_agent, ip_address)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
		session.ID, session.AccountID, session.TokenHash, string(session.Status), session.IssuedAt,
		session.ExpiresAt, session.UserAgent, session.IPAddress)
	return session, err
}

func (r *IdentityRepository) GetSessionByTokenHash(ctx context.Context, tokenHash string, now time.Time) (identity.Session, identity.Account, error) {
	row := r.db.QueryRowContext(ctx, `
SELECT ws.session_id, ws.account_id, ws.token_hash, ws.status, ws.issued_at, ws.expires_at,
       ws.revoked_at, ws.last_seen_at, ws.user_agent, ws.ip_address,
       a.account_id, a.username, a.username_norm, a.display_name, COALESCE(a.email, ''), COALESCE(a.phone, ''),
       a.role, a.status, a.password_hash, a.password_algo, a.password_params::text, a.created_via,
       COALESCE(a.created_by, ''), a.created_at, a.updated_at, a.disabled_at, a.deleted_at
FROM web_sessions ws
JOIN accounts a ON a.account_id = ws.account_id
WHERE ws.token_hash = $1`, tokenHash)
	session, account, err := scanSessionAndAccount(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.Session{}, identity.Account{}, errorsx.Unauthorized("invalid session")
		}
		return identity.Session{}, identity.Account{}, err
	}
	if session.Status != identity.SessionStatusActive || !session.ExpiresAt.After(now) {
		return identity.Session{}, identity.Account{}, errorsx.Unauthorized("invalid session")
	}
	_, _ = r.db.ExecContext(ctx, `UPDATE web_sessions SET last_seen_at = $2 WHERE session_id = $1`, session.ID, now)
	return session, account, nil
}

func (r *IdentityRepository) RevokeSession(ctx context.Context, sessionID string) (identity.Session, error) {
	_, err := r.db.ExecContext(ctx, `
UPDATE web_sessions
SET status = 'revoked', revoked_at = COALESCE(revoked_at, now())
WHERE session_id = $1`, sessionID)
	if err != nil {
		return identity.Session{}, err
	}
	row := r.db.QueryRowContext(ctx, `
SELECT session_id, account_id, token_hash, status, issued_at, expires_at, revoked_at, last_seen_at, user_agent, ip_address
FROM web_sessions
WHERE session_id = $1`, sessionID)
	session, err := scanSession(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.Session{}, errorsx.NotFound("session not found")
		}
		return identity.Session{}, err
	}
	return session, nil
}

func (r *IdentityRepository) CreateAccessAccount(ctx context.Context, create identity.AccessAccountCreate) (identity.AccessAccount, error) {
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	defer rollback(tx)
	owner, err := getIdentityAccountTx(ctx, tx, create.OwnerAccountID)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if owner.Status == identity.AccountStatusDisabled {
		return identity.AccessAccount{}, errorsx.New(403, "account_disabled", "account is disabled")
	}
	if owner.Status == identity.AccountStatusDeleted {
		return identity.AccessAccount{}, errorsx.New(403, "account_deleted", "account is deleted")
	}
	result, err := tx.ExecContext(ctx, `
INSERT INTO access_name_locks (username_norm, owner_account_id, locked_reason)
VALUES ($1, $2, 'registered')
ON CONFLICT (username_norm) DO NOTHING`, create.UsernameNorm, create.OwnerAccountID)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if rows == 0 {
		return identity.AccessAccount{}, errorsx.New(409, "access_username_reserved", "access account username is reserved")
	}
	version, err := nextAuthProjectionVersion(ctx, tx)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	paramsJSON, err := json.Marshal(emptyMap(create.PasswordParams))
	if err != nil {
		return identity.AccessAccount{}, errorsx.BadRequest("password params must be JSON serializable")
	}
	policyJSON, err := json.Marshal(emptyMap(create.AuthPolicy))
	if err != nil {
		return identity.AccessAccount{}, errorsx.BadRequest("auth_policy must be JSON serializable")
	}
	accessID := identity.NewID("aacc")
	_, err = tx.ExecContext(ctx, `
INSERT INTO access_accounts
  (access_account_id, owner_account_id, username, username_norm, display_name, note, status,
   password_hash, password_algo, password_params, expires_at, concurrency_limit, auth_policy,
   projection_version, created_by, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, $6, 'active', $7, $8, $9::jsonb, $10, $11, $12::jsonb, $13, $14, now(), now())`,
		accessID, create.OwnerAccountID, create.Username, create.UsernameNorm, create.DisplayName, create.Note,
		create.PasswordHash, create.PasswordAlgo, string(paramsJSON), nullableTimePtr(create.ExpiresAt),
		create.ConcurrencyLimit, string(policyJSON), version, nullableString(create.CreatedBy))
	if err != nil {
		if isUniqueViolation(err) {
			return identity.AccessAccount{}, errorsx.New(409, "access_username_reserved", "access account username is reserved")
		}
		return identity.AccessAccount{}, err
	}
	if _, err := tx.ExecContext(ctx, `UPDATE access_name_locks SET access_account_id = $2 WHERE username_norm = $1`, create.UsernameNorm, accessID); err != nil {
		return identity.AccessAccount{}, err
	}
	if err := tx.Commit(); err != nil {
		return identity.AccessAccount{}, err
	}
	return r.GetAccessAccount(ctx, accessID)
}

func (r *IdentityRepository) GetAccessAccount(ctx context.Context, accessAccountID string) (identity.AccessAccount, error) {
	row := r.db.QueryRowContext(ctx, accessAccountSelectSQL()+` WHERE aa.access_account_id = $1`, accessAccountID)
	access, err := scanIdentityAccessAccount(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.AccessAccount{}, errorsx.NotFound("access account not found")
		}
		return identity.AccessAccount{}, err
	}
	return access, nil
}

func (r *IdentityRepository) ListAccessAccounts(ctx context.Context, filter identity.AccessAccountListFilter) ([]identity.AccessAccount, int, error) {
	where, args := accessAccountFilterSQL(filter)
	var total int
	if err := r.db.QueryRowContext(ctx, `SELECT count(*) FROM access_accounts aa JOIN accounts owner ON owner.account_id = aa.owner_account_id`+where, args...).Scan(&total); err != nil {
		return nil, 0, err
	}
	args = append(args, filter.Limit, filter.Offset)
	query := accessAccountSelectSQL() + where + fmt.Sprintf(` ORDER BY aa.created_at DESC, aa.access_account_id DESC LIMIT $%d OFFSET $%d`, len(args)-1, len(args))
	rows, err := r.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, 0, err
	}
	defer rows.Close()
	var out []identity.AccessAccount
	for rows.Next() {
		access, err := scanIdentityAccessAccount(rows)
		if err != nil {
			return nil, 0, err
		}
		out = append(out, access)
	}
	return out, total, rows.Err()
}

func (r *IdentityRepository) UpdateAccessAccount(ctx context.Context, accessAccountID string, ownerAccountID string, update identity.AccessAccountUpdate) (identity.AccessAccount, error) {
	policyJSON, err := json.Marshal(emptyMap(update.AuthPolicy))
	if err != nil {
		return identity.AccessAccount{}, errorsx.BadRequest("auth_policy must be JSON serializable")
	}
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	defer rollback(tx)
	version, err := nextAuthProjectionVersion(ctx, tx)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	result, err := tx.ExecContext(ctx, `
UPDATE access_accounts
SET display_name = $3,
    note = $4,
    expires_at = $5,
    concurrency_limit = $6,
    auth_policy = $7::jsonb,
    projection_version = $8,
    updated_at = now()
WHERE access_account_id = $1
  AND ($2 = '' OR owner_account_id = $2)
  AND status <> 'deleted'`,
		accessAccountID, ownerAccountID, update.DisplayName, update.Note, nullableTimePtr(update.ExpiresAt),
		update.ConcurrencyLimit, string(policyJSON), version)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if rows == 0 {
		return identity.AccessAccount{}, errorsx.NotFound("access account not found")
	}
	if err := tx.Commit(); err != nil {
		return identity.AccessAccount{}, err
	}
	return r.GetAccessAccount(ctx, accessAccountID)
}

func (r *IdentityRepository) SetAccessAccountPassword(ctx context.Context, accessAccountID string, ownerAccountID string, passwordHash string, passwordAlgo string, passwordParams map[string]any) (identity.AccessAccount, error) {
	paramsJSON, err := json.Marshal(emptyMap(passwordParams))
	if err != nil {
		return identity.AccessAccount{}, errorsx.BadRequest("password params must be JSON serializable")
	}
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	defer rollback(tx)
	version, err := nextAuthProjectionVersion(ctx, tx)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	result, err := tx.ExecContext(ctx, `
UPDATE access_accounts
SET password_hash = $3,
    password_algo = $4,
    password_params = $5::jsonb,
    projection_version = $6,
    updated_at = now()
WHERE access_account_id = $1
  AND ($2 = '' OR owner_account_id = $2)
  AND status <> 'deleted'`,
		accessAccountID, ownerAccountID, passwordHash, passwordAlgo, string(paramsJSON), version)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if rows == 0 {
		return identity.AccessAccount{}, errorsx.NotFound("access account not found")
	}
	if err := tx.Commit(); err != nil {
		return identity.AccessAccount{}, err
	}
	return r.GetAccessAccount(ctx, accessAccountID)
}

func (r *IdentityRepository) SetAccessAccountStatus(ctx context.Context, accessAccountID string, ownerAccountID string, status identity.AccessStatus) (identity.AccessAccount, error) {
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	defer rollback(tx)
	version, err := nextAuthProjectionVersion(ctx, tx)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	result, err := tx.ExecContext(ctx, `
UPDATE access_accounts
SET status = $3,
    disabled_at = CASE WHEN $3 = 'disabled' THEN now() ELSE disabled_at END,
    projection_version = $4,
    updated_at = now()
WHERE access_account_id = $1
  AND ($2 = '' OR owner_account_id = $2)
  AND status <> 'deleted'`,
		accessAccountID, ownerAccountID, string(status), version)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if rows == 0 {
		return identity.AccessAccount{}, errorsx.NotFound("access account not found")
	}
	if err := tx.Commit(); err != nil {
		return identity.AccessAccount{}, err
	}
	return r.GetAccessAccount(ctx, accessAccountID)
}

func (r *IdentityRepository) DeleteAccessAccount(ctx context.Context, accessAccountID string, ownerAccountID string) (identity.AccessAccount, error) {
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	defer rollback(tx)
	access, err := getIdentityAccessAccountTx(ctx, tx, accessAccountID, ownerAccountID)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if access.Status == identity.AccessStatusDeleted {
		return identity.AccessAccount{}, errorsx.New(409, "access_account_deleted", "access account is deleted")
	}
	version, err := nextAuthProjectionVersion(ctx, tx)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	_, err = tx.ExecContext(ctx, `
UPDATE access_accounts
SET status = 'deleted',
    deleted_at = now(),
    projection_version = $3,
    updated_at = now()
WHERE access_account_id = $1 AND ($2 = '' OR owner_account_id = $2)`,
		accessAccountID, ownerAccountID, version)
	if err != nil {
		return identity.AccessAccount{}, err
	}
	if _, err := tx.ExecContext(ctx, `UPDATE access_name_locks SET locked_reason = 'deleted' WHERE username_norm = $1`, access.UsernameNorm); err != nil {
		return identity.AccessAccount{}, err
	}
	if err := tx.Commit(); err != nil {
		return identity.AccessAccount{}, err
	}
	return r.GetAccessAccount(ctx, accessAccountID)
}

func (r *IdentityRepository) WriteAudit(ctx context.Context, entry identity.AuditEntry) error {
	beforeJSON, err := nullableJSON(entry.BeforeData)
	if err != nil {
		return errorsx.BadRequest("audit before_data must be JSON serializable")
	}
	afterJSON, err := nullableJSON(entry.AfterData)
	if err != nil {
		return errorsx.BadRequest("audit after_data must be JSON serializable")
	}
	_, err = r.db.ExecContext(ctx, `
INSERT INTO audit_logs
  (audit_id, actor_account_id, actor_username, action, target_type, target_id,
   before_data, after_data, request_id, ip_address, user_agent, created_at)
VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8::jsonb, $9, $10, $11, now())`,
		identity.NewID("aud"), nullableString(entry.ActorAccountID), entry.ActorUsername, entry.Action,
		entry.TargetType, entry.TargetID, beforeJSON, afterJSON, entry.RequestID, entry.IPAddress, entry.UserAgent)
	return err
}

func accountSelectSQL() string {
	return `
SELECT account_id, username, username_norm, display_name, COALESCE(email, ''), COALESCE(phone, ''),
       role, status, password_hash, password_algo, password_params::text, created_via,
       COALESCE(created_by, ''), created_at, updated_at, disabled_at, deleted_at
FROM accounts`
}

func accessAccountSelectSQL() string {
	return `
SELECT aa.access_account_id, aa.owner_account_id, owner.username, owner.status,
       aa.username, aa.username_norm, aa.display_name, aa.note, aa.status,
       aa.password_hash, aa.password_algo, aa.password_params::text, aa.expires_at,
       aa.concurrency_limit, aa.auth_policy::text, aa.projection_version, COALESCE(aa.created_by, ''),
       aa.created_at, aa.updated_at, aa.disabled_at, aa.deleted_at
FROM access_accounts aa
JOIN accounts owner ON owner.account_id = aa.owner_account_id`
}

func accountFilterSQL(filter identity.AccountListFilter) (string, []any) {
	var where []string
	var args []any
	if filter.Status != "" {
		args = append(args, string(filter.Status))
		where = append(where, fmt.Sprintf("status = $%d", len(args)))
	}
	if filter.Role != "" {
		args = append(args, string(filter.Role))
		where = append(where, fmt.Sprintf("role = $%d", len(args)))
	}
	if strings.TrimSpace(filter.Search) != "" {
		args = append(args, "%"+strings.ToLower(strings.TrimSpace(filter.Search))+"%")
		where = append(where, fmt.Sprintf("(lower(username) LIKE $%d OR lower(display_name) LIKE $%d)", len(args), len(args)))
	}
	if len(where) == 0 {
		return "", args
	}
	return " WHERE " + strings.Join(where, " AND "), args
}

func accessAccountFilterSQL(filter identity.AccessAccountListFilter) (string, []any) {
	var where []string
	var args []any
	if filter.OwnerAccountID != "" {
		args = append(args, filter.OwnerAccountID)
		where = append(where, fmt.Sprintf("aa.owner_account_id = $%d", len(args)))
	}
	if filter.Status != "" {
		args = append(args, string(filter.Status))
		where = append(where, fmt.Sprintf("aa.status = $%d", len(args)))
	}
	if strings.TrimSpace(filter.Search) != "" {
		args = append(args, "%"+strings.ToLower(strings.TrimSpace(filter.Search))+"%")
		where = append(where, fmt.Sprintf("(lower(aa.username) LIKE $%d OR lower(aa.display_name) LIKE $%d)", len(args), len(args)))
	}
	if len(where) == 0 {
		return "", args
	}
	return " WHERE " + strings.Join(where, " AND "), args
}

func getIdentityAccountTx(ctx context.Context, tx *sql.Tx, accountID string) (identity.Account, error) {
	row := tx.QueryRowContext(ctx, accountSelectSQL()+` WHERE account_id = $1 FOR UPDATE`, accountID)
	account, err := scanIdentityAccount(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.Account{}, errorsx.NotFound("account not found")
		}
		return identity.Account{}, err
	}
	return account, nil
}

func getIdentityAccessAccountTx(ctx context.Context, tx *sql.Tx, accessAccountID string, ownerAccountID string) (identity.AccessAccount, error) {
	row := tx.QueryRowContext(ctx, accessAccountSelectSQL()+` WHERE aa.access_account_id = $1 AND ($2 = '' OR aa.owner_account_id = $2) FOR UPDATE OF aa`, accessAccountID, ownerAccountID)
	access, err := scanIdentityAccessAccount(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return identity.AccessAccount{}, errorsx.NotFound("access account not found")
		}
		return identity.AccessAccount{}, err
	}
	return access, nil
}

func listAccessAccountsTx(ctx context.Context, tx *sql.Tx, where string, args ...any) ([]identity.AccessAccount, error) {
	rows, err := tx.QueryContext(ctx, accessAccountSelectSQL()+" "+where, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []identity.AccessAccount
	for rows.Next() {
		access, err := scanIdentityAccessAccount(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, access)
	}
	return out, rows.Err()
}

func scanIdentityAccount(row scanner) (identity.Account, error) {
	var account identity.Account
	var role, status, createdVia string
	var paramsRaw string
	var disabledAt, deletedAt sql.NullTime
	if err := row.Scan(
		&account.ID, &account.Username, &account.UsernameNorm, &account.DisplayName, &account.Email, &account.Phone,
		&role, &status, &account.PasswordHash, &account.PasswordAlgo, &paramsRaw, &createdVia,
		&account.CreatedBy, &account.CreatedAt, &account.UpdatedAt, &disabledAt, &deletedAt,
	); err != nil {
		return identity.Account{}, err
	}
	populateIdentityAccount(&account, role, status, createdVia, paramsRaw, disabledAt, deletedAt)
	return account, nil
}

func scanIdentityAccountDetail(row scanner) (identity.Account, int, int, error) {
	var account identity.Account
	var role, status, createdVia string
	var paramsRaw string
	var disabledAt, deletedAt sql.NullTime
	var accessCount, sessionCount int64
	if err := row.Scan(
		&account.ID, &account.Username, &account.UsernameNorm, &account.DisplayName, &account.Email, &account.Phone,
		&role, &status, &account.PasswordHash, &account.PasswordAlgo, &paramsRaw, &createdVia,
		&account.CreatedBy, &account.CreatedAt, &account.UpdatedAt, &disabledAt, &deletedAt,
		&accessCount, &sessionCount,
	); err != nil {
		return identity.Account{}, 0, 0, err
	}
	populateIdentityAccount(&account, role, status, createdVia, paramsRaw, disabledAt, deletedAt)
	return account, int(accessCount), int(sessionCount), nil
}

func populateIdentityAccount(account *identity.Account, role string, status string, createdVia string, paramsRaw string, disabledAt sql.NullTime, deletedAt sql.NullTime) {
	account.Role = identity.Role(role)
	account.Status = identity.AccountStatus(status)
	account.CreatedVia = identity.CreatedVia(createdVia)
	account.PasswordParams = parseJSONMap(paramsRaw)
	if disabledAt.Valid {
		account.DisabledAt = &disabledAt.Time
	}
	if deletedAt.Valid {
		account.DeletedAt = &deletedAt.Time
	}
}

func scanIdentityAccessAccount(row scanner) (identity.AccessAccount, error) {
	var access identity.AccessAccount
	var ownerStatus, status string
	var paramsRaw, policyRaw string
	var expiresAt, disabledAt, deletedAt sql.NullTime
	if err := row.Scan(
		&access.ID, &access.OwnerAccountID, &access.OwnerUsername, &ownerStatus,
		&access.Username, &access.UsernameNorm, &access.DisplayName, &access.Note, &status,
		&access.PasswordHash, &access.PasswordAlgo, &paramsRaw, &expiresAt,
		&access.ConcurrencyLimit, &policyRaw, &access.ProjectionVersion, &access.CreatedBy,
		&access.CreatedAt, &access.UpdatedAt, &disabledAt, &deletedAt,
	); err != nil {
		return identity.AccessAccount{}, err
	}
	access.OwnerStatus = identity.AccountStatus(ownerStatus)
	access.Status = identity.AccessStatus(status)
	access.PasswordParams = parseJSONMap(paramsRaw)
	access.AuthPolicy = parseJSONMap(policyRaw)
	if expiresAt.Valid {
		access.ExpiresAt = &expiresAt.Time
	}
	if disabledAt.Valid {
		access.DisabledAt = &disabledAt.Time
	}
	if deletedAt.Valid {
		access.DeletedAt = &deletedAt.Time
	}
	return access, nil
}

func scanSession(row scanner) (identity.Session, error) {
	var session identity.Session
	var status string
	var revokedAt, lastSeenAt sql.NullTime
	err := row.Scan(
		&session.ID, &session.AccountID, &session.TokenHash, &status, &session.IssuedAt,
		&session.ExpiresAt, &revokedAt, &lastSeenAt, &session.UserAgent, &session.IPAddress,
	)
	if err != nil {
		return identity.Session{}, err
	}
	session.Status = identity.SessionStatus(status)
	if revokedAt.Valid {
		session.RevokedAt = &revokedAt.Time
	}
	if lastSeenAt.Valid {
		session.LastSeenAt = &lastSeenAt.Time
	}
	return session, nil
}

func scanSessionAndAccount(row scanner) (identity.Session, identity.Account, error) {
	var session identity.Session
	var sessionStatus string
	var revokedAt, lastSeenAt sql.NullTime
	var account identity.Account
	var role, accountStatus, createdVia string
	var paramsRaw string
	var disabledAt, deletedAt sql.NullTime
	err := row.Scan(
		&session.ID, &session.AccountID, &session.TokenHash, &sessionStatus, &session.IssuedAt,
		&session.ExpiresAt, &revokedAt, &lastSeenAt, &session.UserAgent, &session.IPAddress,
		&account.ID, &account.Username, &account.UsernameNorm, &account.DisplayName, &account.Email, &account.Phone,
		&role, &accountStatus, &account.PasswordHash, &account.PasswordAlgo, &paramsRaw, &createdVia,
		&account.CreatedBy, &account.CreatedAt, &account.UpdatedAt, &disabledAt, &deletedAt,
	)
	if err != nil {
		return identity.Session{}, identity.Account{}, err
	}
	session.Status = identity.SessionStatus(sessionStatus)
	if revokedAt.Valid {
		session.RevokedAt = &revokedAt.Time
	}
	if lastSeenAt.Valid {
		session.LastSeenAt = &lastSeenAt.Time
	}
	account.Role = identity.Role(role)
	account.Status = identity.AccountStatus(accountStatus)
	account.CreatedVia = identity.CreatedVia(createdVia)
	account.PasswordParams = parseJSONMap(paramsRaw)
	if disabledAt.Valid {
		account.DisabledAt = &disabledAt.Time
	}
	if deletedAt.Valid {
		account.DeletedAt = &deletedAt.Time
	}
	return session, account, nil
}

func nextAuthProjectionVersion(ctx context.Context, tx *sql.Tx) (int64, error) {
	var version int64
	err := tx.QueryRowContext(ctx, `SELECT nextval('auth_projection_version_seq')`).Scan(&version)
	return version, err
}

func nullableTimePtr(value *time.Time) any {
	if value == nil {
		return nil
	}
	return *value
}

func nullableJSON(value map[string]any) (any, error) {
	if value == nil {
		return nil, nil
	}
	payload, err := json.Marshal(value)
	if err != nil {
		return nil, err
	}
	return string(payload), nil
}

func emptyMap(value map[string]any) map[string]any {
	if value == nil {
		return map[string]any{}
	}
	return value
}

func parseJSONMap(raw string) map[string]any {
	if raw == "" {
		return map[string]any{}
	}
	var out map[string]any
	if err := json.Unmarshal([]byte(raw), &out); err != nil || out == nil {
		return map[string]any{}
	}
	return out
}

func isUniqueViolation(err error) bool {
	pqErr, ok := err.(*pq.Error)
	return ok && pqErr.Code == "23505"
}
