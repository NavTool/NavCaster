package identity

import (
	"context"
	"strings"
	"sync"
	"time"

	"navcaster-admin/internal/errorsx"
)

type MemoryRepository struct {
	mu               sync.Mutex
	accounts         map[string]Account
	accountNameLocks map[string]string
	accessAccounts   map[string]AccessAccount
	accessNameLocks  map[string]string
	sessions         map[string]Session
	sessionByToken   map[string]string
	audits           []AuditEntry
	projectionSeq    int64
}

func NewMemoryRepository() *MemoryRepository {
	return &MemoryRepository{
		accounts:         map[string]Account{},
		accountNameLocks: map[string]string{},
		accessAccounts:   map[string]AccessAccount{},
		accessNameLocks:  map[string]string{},
		sessions:         map[string]Session{},
		sessionByToken:   map[string]string{},
	}
}

func (r *MemoryRepository) CreateAccount(_ context.Context, create AccountCreate) (Account, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, exists := r.accountNameLocks[create.UsernameNorm]; exists {
		return Account{}, errorsx.New(409, "username_reserved", "username is already reserved")
	}
	now := time.Now().UTC()
	account := Account{
		ID:             NewID("acc"),
		Username:       strings.TrimSpace(create.Username),
		UsernameNorm:   create.UsernameNorm,
		DisplayName:    create.DisplayName,
		Email:          create.Email,
		Phone:          create.Phone,
		Role:           firstRole(create.Role),
		Status:         firstAccountStatus(create.Status),
		PasswordHash:   create.PasswordHash,
		PasswordAlgo:   create.PasswordAlgo,
		PasswordParams: cloneMap(create.PasswordParams),
		CreatedVia:     firstCreatedVia(create.CreatedVia),
		CreatedBy:      create.CreatedBy,
		CreatedAt:      now,
		UpdatedAt:      now,
	}
	r.accounts[account.ID] = account
	r.accountNameLocks[account.UsernameNorm] = account.ID
	return cloneAccount(account), nil
}

func (r *MemoryRepository) GetAccount(_ context.Context, accountID string) (Account, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	account, ok := r.accounts[accountID]
	if !ok {
		return Account{}, errorsx.NotFound("account not found")
	}
	return cloneAccount(account), nil
}

func (r *MemoryRepository) GetAccountByUsernameNorm(_ context.Context, usernameNorm string) (Account, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for _, account := range r.accounts {
		if account.UsernameNorm == usernameNorm {
			return cloneAccount(account), nil
		}
	}
	return Account{}, errorsx.NotFound("account not found")
}

func (r *MemoryRepository) GetAccountDetail(_ context.Context, accountID string) (AccountDetail, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	account, ok := r.accounts[accountID]
	if !ok {
		return AccountDetail{}, errorsx.NotFound("account not found")
	}
	detail := AccountDetail{Account: cloneAccount(account)}
	now := time.Now().UTC()
	for _, access := range r.accessAccounts {
		if access.OwnerAccountID == accountID && access.Status != AccessStatusDeleted {
			detail.AccessAccountCount++
		}
	}
	for _, session := range r.sessions {
		if session.AccountID == accountID && session.Status == SessionStatusActive && session.ExpiresAt.After(now) {
			detail.ActiveSessionCount++
		}
	}
	return detail, nil
}

func (r *MemoryRepository) ListAccounts(_ context.Context, filter AccountListFilter) ([]Account, int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	var out []Account
	search := strings.ToLower(strings.TrimSpace(filter.Search))
	for _, account := range r.accounts {
		if filter.Status != "" && account.Status != filter.Status {
			continue
		}
		if filter.Role != "" && account.Role != filter.Role {
			continue
		}
		if search != "" && !strings.Contains(strings.ToLower(account.Username), search) && !strings.Contains(strings.ToLower(account.DisplayName), search) {
			continue
		}
		out = append(out, cloneAccount(account))
	}
	return sliceAccounts(out, filter.Limit, filter.Offset), len(out), nil
}

func (r *MemoryRepository) UpdateAccountProfile(_ context.Context, accountID string, update AccountProfileUpdate) (Account, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	account, ok := r.accounts[accountID]
	if !ok {
		return Account{}, errorsx.NotFound("account not found")
	}
	if account.Status == AccountStatusDeleted {
		return Account{}, errorsx.New(409, "account_deleted", "account is deleted")
	}
	account.DisplayName = update.DisplayName
	account.Email = update.Email
	account.Phone = update.Phone
	account.UpdatedAt = time.Now().UTC()
	r.accounts[accountID] = account
	return cloneAccount(account), nil
}

func (r *MemoryRepository) SetAccountStatus(_ context.Context, accountID string, status AccountStatus) (Account, []AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	account, ok := r.accounts[accountID]
	if !ok {
		return Account{}, nil, errorsx.NotFound("account not found")
	}
	if account.Status == AccountStatusDeleted {
		return Account{}, nil, errorsx.New(409, "invalid_state_transition", "deleted account cannot change status")
	}
	switch status {
	case AccountStatusActive, AccountStatusDisabled, AccountStatusDeleted:
	default:
		return Account{}, nil, errorsx.BadRequest("unsupported account status")
	}
	now := time.Now().UTC()
	account.Status = status
	account.UpdatedAt = now
	if status == AccountStatusDisabled {
		account.DisabledAt = &now
	}
	if status == AccountStatusDeleted {
		account.DeletedAt = &now
		r.accountNameLocks[account.UsernameNorm] = account.ID
	}
	r.accounts[accountID] = account
	if status == AccountStatusDisabled || status == AccountStatusDeleted {
		for id, session := range r.sessions {
			if session.AccountID == accountID && session.Status == SessionStatusActive {
				session.Status = SessionStatusRevoked
				session.RevokedAt = &now
				r.sessions[id] = session
			}
		}
	}
	var affected []AccessAccount
	for id, access := range r.accessAccounts {
		if access.OwnerAccountID != accountID || access.Status == AccessStatusDeleted {
			continue
		}
		if status == AccountStatusDeleted && access.Status != AccessStatusDisabled {
			access.Status = AccessStatusDisabled
			access.DisabledAt = &now
			access.UpdatedAt = now
			r.projectionSeq++
			access.ProjectionVersion = r.projectionSeq
			r.accessAccounts[id] = access
		}
		access.OwnerStatus = status
		affected = append(affected, cloneAccessAccount(access))
	}
	return cloneAccount(account), affected, nil
}

func (r *MemoryRepository) SetAccountPassword(_ context.Context, accountID string, passwordHash string, passwordAlgo string, passwordParams map[string]any, revokeSessions bool) (Account, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	account, ok := r.accounts[accountID]
	if !ok {
		return Account{}, errorsx.NotFound("account not found")
	}
	if account.Status == AccountStatusDeleted {
		return Account{}, errorsx.New(409, "account_deleted", "account is deleted")
	}
	account.PasswordHash = passwordHash
	account.PasswordAlgo = passwordAlgo
	account.PasswordParams = cloneMap(passwordParams)
	account.UpdatedAt = time.Now().UTC()
	r.accounts[accountID] = account
	if revokeSessions {
		now := time.Now().UTC()
		for id, session := range r.sessions {
			if session.AccountID == accountID && session.Status == SessionStatusActive {
				session.Status = SessionStatusRevoked
				session.RevokedAt = &now
				r.sessions[id] = session
			}
		}
	}
	return cloneAccount(account), nil
}

func (r *MemoryRepository) RevokeSessionsForAccount(_ context.Context, accountID string) (int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := time.Now().UTC()
	count := 0
	for id, session := range r.sessions {
		if session.AccountID == accountID && session.Status == SessionStatusActive {
			session.Status = SessionStatusRevoked
			session.RevokedAt = &now
			r.sessions[id] = session
			count++
		}
	}
	return count, nil
}

func (r *MemoryRepository) CreateSession(_ context.Context, session Session) (Session, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.sessions[session.ID] = session
	r.sessionByToken[session.TokenHash] = session.ID
	return session, nil
}

func (r *MemoryRepository) GetSessionByTokenHash(_ context.Context, tokenHash string, now time.Time) (Session, Account, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	sessionID, ok := r.sessionByToken[tokenHash]
	if !ok {
		return Session{}, Account{}, errorsx.Unauthorized("invalid session")
	}
	session := r.sessions[sessionID]
	if session.Status != SessionStatusActive || !session.ExpiresAt.After(now) {
		return Session{}, Account{}, errorsx.Unauthorized("invalid session")
	}
	account, ok := r.accounts[session.AccountID]
	if !ok {
		return Session{}, Account{}, errorsx.Unauthorized("invalid session")
	}
	session.LastSeenAt = &now
	r.sessions[sessionID] = session
	return session, cloneAccount(account), nil
}

func (r *MemoryRepository) RevokeSession(_ context.Context, sessionID string) (Session, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	session, ok := r.sessions[sessionID]
	if !ok {
		return Session{}, errorsx.NotFound("session not found")
	}
	now := time.Now().UTC()
	session.Status = SessionStatusRevoked
	session.RevokedAt = &now
	r.sessions[sessionID] = session
	return session, nil
}

func (r *MemoryRepository) CreateAccessAccount(_ context.Context, create AccessAccountCreate) (AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	owner, ok := r.accounts[create.OwnerAccountID]
	if !ok {
		return AccessAccount{}, errorsx.NotFound("owner account not found")
	}
	if owner.Status == AccountStatusDisabled {
		return AccessAccount{}, errorsx.New(403, "account_disabled", "account is disabled")
	}
	if owner.Status == AccountStatusDeleted {
		return AccessAccount{}, errorsx.New(403, "account_deleted", "account is deleted")
	}
	if _, exists := r.accessNameLocks[create.UsernameNorm]; exists {
		return AccessAccount{}, errorsx.New(409, "access_username_reserved", "access account username is reserved")
	}
	now := time.Now().UTC()
	r.projectionSeq++
	access := AccessAccount{
		ID:                NewID("aacc"),
		OwnerAccountID:    create.OwnerAccountID,
		OwnerUsername:     owner.Username,
		OwnerStatus:       owner.Status,
		Username:          strings.TrimSpace(create.Username),
		UsernameNorm:      create.UsernameNorm,
		DisplayName:       create.DisplayName,
		Note:              create.Note,
		Status:            AccessStatusActive,
		PasswordHash:      create.PasswordHash,
		PasswordAlgo:      create.PasswordAlgo,
		PasswordParams:    cloneMap(create.PasswordParams),
		ExpiresAt:         cloneTimePtr(create.ExpiresAt),
		ConcurrencyLimit:  create.ConcurrencyLimit,
		AuthPolicy:        cloneMap(create.AuthPolicy),
		ProjectionVersion: r.projectionSeq,
		CreatedBy:         create.CreatedBy,
		CreatedAt:         now,
		UpdatedAt:         now,
	}
	r.accessAccounts[access.ID] = access
	r.accessNameLocks[access.UsernameNorm] = access.ID
	return cloneAccessAccount(access), nil
}

func (r *MemoryRepository) GetAccessAccount(_ context.Context, accessAccountID string) (AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	access, ok := r.accessAccounts[accessAccountID]
	if !ok {
		return AccessAccount{}, errorsx.NotFound("access account not found")
	}
	return cloneAccessAccount(r.withOwner(access)), nil
}

func (r *MemoryRepository) ListAccessAccounts(_ context.Context, filter AccessAccountListFilter) ([]AccessAccount, int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	search := strings.ToLower(strings.TrimSpace(filter.Search))
	var out []AccessAccount
	for _, access := range r.accessAccounts {
		if filter.OwnerAccountID != "" && access.OwnerAccountID != filter.OwnerAccountID {
			continue
		}
		if filter.Status != "" && access.Status != filter.Status {
			continue
		}
		if search != "" && !strings.Contains(strings.ToLower(access.Username), search) && !strings.Contains(strings.ToLower(access.DisplayName), search) {
			continue
		}
		out = append(out, cloneAccessAccount(r.withOwner(access)))
	}
	return sliceAccessAccounts(out, filter.Limit, filter.Offset), len(out), nil
}

func (r *MemoryRepository) UpdateAccessAccount(_ context.Context, accessAccountID string, ownerAccountID string, update AccessAccountUpdate) (AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	access, err := r.mutableAccess(accessAccountID, ownerAccountID)
	if err != nil {
		return AccessAccount{}, err
	}
	access.DisplayName = update.DisplayName
	access.Note = update.Note
	if update.ClearExpiresAt {
		access.ExpiresAt = nil
	} else if update.ExpiresAt != nil {
		access.ExpiresAt = cloneTimePtr(update.ExpiresAt)
	}
	access.ConcurrencyLimit = update.ConcurrencyLimit
	access.AuthPolicy = cloneMap(update.AuthPolicy)
	access.UpdatedAt = time.Now().UTC()
	r.projectionSeq++
	access.ProjectionVersion = r.projectionSeq
	r.accessAccounts[access.ID] = access
	return cloneAccessAccount(r.withOwner(access)), nil
}

func (r *MemoryRepository) SetAccessAccountPassword(_ context.Context, accessAccountID string, ownerAccountID string, passwordHash string, passwordAlgo string, passwordParams map[string]any) (AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	access, err := r.mutableAccess(accessAccountID, ownerAccountID)
	if err != nil {
		return AccessAccount{}, err
	}
	access.PasswordHash = passwordHash
	access.PasswordAlgo = passwordAlgo
	access.PasswordParams = cloneMap(passwordParams)
	access.UpdatedAt = time.Now().UTC()
	r.projectionSeq++
	access.ProjectionVersion = r.projectionSeq
	r.accessAccounts[access.ID] = access
	return cloneAccessAccount(r.withOwner(access)), nil
}

func (r *MemoryRepository) SetAccessAccountStatus(_ context.Context, accessAccountID string, ownerAccountID string, status AccessStatus) (AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if status != AccessStatusActive && status != AccessStatusDisabled {
		return AccessAccount{}, errorsx.New(409, "invalid_state_transition", "unsupported access account status")
	}
	access, err := r.mutableAccess(accessAccountID, ownerAccountID)
	if err != nil {
		return AccessAccount{}, err
	}
	now := time.Now().UTC()
	access.Status = status
	access.UpdatedAt = now
	if status == AccessStatusDisabled {
		access.DisabledAt = &now
	}
	r.projectionSeq++
	access.ProjectionVersion = r.projectionSeq
	r.accessAccounts[access.ID] = access
	return cloneAccessAccount(r.withOwner(access)), nil
}

func (r *MemoryRepository) DeleteAccessAccount(_ context.Context, accessAccountID string, ownerAccountID string) (AccessAccount, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	access, err := r.mutableAccess(accessAccountID, ownerAccountID)
	if err != nil {
		return AccessAccount{}, err
	}
	now := time.Now().UTC()
	access.Status = AccessStatusDeleted
	access.DeletedAt = &now
	access.UpdatedAt = now
	r.projectionSeq++
	access.ProjectionVersion = r.projectionSeq
	r.accessAccounts[access.ID] = access
	r.accessNameLocks[access.UsernameNorm] = access.ID
	return cloneAccessAccount(r.withOwner(access)), nil
}

func (r *MemoryRepository) WriteAudit(_ context.Context, entry AuditEntry) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.audits = append(r.audits, entry)
	return nil
}

func (r *MemoryRepository) mutableAccess(accessAccountID string, ownerAccountID string) (AccessAccount, error) {
	access, ok := r.accessAccounts[accessAccountID]
	if !ok || (ownerAccountID != "" && access.OwnerAccountID != ownerAccountID) {
		return AccessAccount{}, errorsx.NotFound("access account not found")
	}
	if access.Status == AccessStatusDeleted {
		return AccessAccount{}, errorsx.New(409, "access_account_deleted", "access account is deleted")
	}
	return access, nil
}

func (r *MemoryRepository) withOwner(access AccessAccount) AccessAccount {
	owner := r.accounts[access.OwnerAccountID]
	access.OwnerUsername = owner.Username
	access.OwnerStatus = owner.Status
	return access
}

func firstRole(role Role) Role {
	if role == "" {
		return RoleUser
	}
	return role
}

func firstAccountStatus(status AccountStatus) AccountStatus {
	if status == "" {
		return AccountStatusActive
	}
	return status
}

func firstCreatedVia(createdVia CreatedVia) CreatedVia {
	if createdVia == "" {
		return CreatedViaAdmin
	}
	return createdVia
}

func cloneAccount(account Account) Account {
	account.PasswordParams = cloneMap(account.PasswordParams)
	account.DisabledAt = cloneTimePtr(account.DisabledAt)
	account.DeletedAt = cloneTimePtr(account.DeletedAt)
	return account
}

func cloneAccessAccount(account AccessAccount) AccessAccount {
	account.PasswordParams = cloneMap(account.PasswordParams)
	account.AuthPolicy = cloneMap(account.AuthPolicy)
	account.ExpiresAt = cloneTimePtr(account.ExpiresAt)
	account.DisabledAt = cloneTimePtr(account.DisabledAt)
	account.DeletedAt = cloneTimePtr(account.DeletedAt)
	return account
}

func cloneMap(in map[string]any) map[string]any {
	if in == nil {
		return map[string]any{}
	}
	out := make(map[string]any, len(in))
	for k, v := range in {
		out[k] = v
	}
	return out
}

func cloneTimePtr(in *time.Time) *time.Time {
	if in == nil {
		return nil
	}
	out := *in
	return &out
}

func sliceAccounts(in []Account, limit int, offset int) []Account {
	if offset < 0 {
		offset = 0
	}
	if limit <= 0 || limit > 200 {
		limit = 50
	}
	if offset >= len(in) {
		return []Account{}
	}
	end := offset + limit
	if end > len(in) {
		end = len(in)
	}
	return in[offset:end]
}

func sliceAccessAccounts(in []AccessAccount, limit int, offset int) []AccessAccount {
	if offset < 0 {
		offset = 0
	}
	if limit <= 0 || limit > 200 {
		limit = 50
	}
	if offset >= len(in) {
		return []AccessAccount{}
	}
	end := offset + limit
	if end > len(in) {
		end = len(in)
	}
	return in[offset:end]
}
