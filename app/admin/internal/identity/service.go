package identity

import (
	"context"
	"strings"
	"time"

	"navcaster-admin/internal/errorsx"
)

const defaultSessionTTL = 24 * time.Hour

type Service struct {
	repo      Repository
	projector AuthProjector
	now       func() time.Time
}

func NewService(repo Repository, projector AuthProjector) Service {
	if projector == nil {
		projector = NoopProjector{}
	}
	return Service{
		repo:      repo,
		projector: projector,
		now:       func() time.Time { return time.Now().UTC() },
	}
}

type RegisterRequest struct {
	Username    string `json:"username"`
	Password    string `json:"password"`
	DisplayName string `json:"display_name"`
}

type LoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type LoginResponse struct {
	Token     string    `json:"token"`
	SessionID string    `json:"session_id"`
	Account   Account   `json:"account"`
	ExpiresAt time.Time `json:"expires_at"`
}

type AdminCreateAccountRequest struct {
	Username    string        `json:"username"`
	Password    string        `json:"password"`
	DisplayName string        `json:"display_name"`
	Email       string        `json:"email"`
	Phone       string        `json:"phone"`
	Role        Role          `json:"role"`
	Status      AccountStatus `json:"status"`
}

type AccountStatusRequest struct {
	Status AccountStatus `json:"status"`
	Reason string        `json:"reason"`
}

type PasswordRequest struct {
	Password       string `json:"password"`
	RevokeSessions bool   `json:"revoke_sessions"`
}

type AccessAccountCreateRequest struct {
	Username         string         `json:"username"`
	Password         string         `json:"password"`
	DisplayName      string         `json:"display_name"`
	Note             string         `json:"note"`
	ExpiresAt        *time.Time     `json:"expires_at"`
	ConcurrencyLimit int            `json:"concurrency_limit"`
	AuthPolicy       map[string]any `json:"auth_policy"`
}

type AccessAccountUpdateRequest struct {
	DisplayName      string         `json:"display_name"`
	Note             string         `json:"note"`
	ExpiresAt        *time.Time     `json:"expires_at"`
	ConcurrencyLimit int            `json:"concurrency_limit"`
	AuthPolicy       map[string]any `json:"auth_policy"`
}

type AccessAccountStatusRequest struct {
	Status AccessStatus `json:"status"`
	Reason string       `json:"reason"`
}

func (s Service) Register(ctx context.Context, req RegisterRequest, meta RequestMeta) (Account, error) {
	usernameNorm, err := validateUsername(req.Username)
	if err != nil {
		return Account{}, err
	}
	hash, algo, params, err := HashPassword(req.Password)
	if err != nil {
		return Account{}, err
	}
	account, err := s.repo.CreateAccount(ctx, AccountCreate{
		Username:       strings.TrimSpace(req.Username),
		UsernameNorm:   usernameNorm,
		DisplayName:    req.DisplayName,
		Role:           RoleUser,
		Status:         AccountStatusActive,
		PasswordHash:   hash,
		PasswordAlgo:   algo,
		PasswordParams: params,
		CreatedVia:     CreatedViaSelfService,
	})
	if err != nil {
		return Account{}, err
	}
	if err := s.audit(ctx, nil, "auth.register", "account", account.ID, nil, map[string]any{"username": account.Username}, meta); err != nil {
		return Account{}, err
	}
	return account, nil
}

func (s Service) Login(ctx context.Context, req LoginRequest, meta RequestMeta) (LoginResponse, error) {
	usernameNorm := NormalizeUsername(req.Username)
	account, err := s.repo.GetAccountByUsernameNorm(ctx, usernameNorm)
	if err != nil {
		return LoginResponse{}, errorsx.Unauthorized("invalid username or password")
	}
	switch account.Status {
	case AccountStatusActive:
	case AccountStatusDisabled:
		return LoginResponse{}, errorsx.New(403, "account_disabled", "account is disabled")
	case AccountStatusDeleted:
		return LoginResponse{}, errorsx.New(403, "account_deleted", "account is deleted")
	default:
		return LoginResponse{}, errorsx.New(403, "account_disabled", "account is not active")
	}
	if !VerifyPassword(req.Password, account.PasswordHash, account.PasswordAlgo, account.PasswordParams) {
		return LoginResponse{}, errorsx.Unauthorized("invalid username or password")
	}
	now := s.now()
	token := NewToken()
	session := Session{
		ID:        NewID("sess"),
		AccountID: account.ID,
		TokenHash: TokenHash(token),
		Status:    SessionStatusActive,
		IssuedAt:  now,
		ExpiresAt: now.Add(defaultSessionTTL),
		UserAgent: meta.UserAgent,
		IPAddress: meta.IPAddress,
	}
	session, err = s.repo.CreateSession(ctx, session)
	if err != nil {
		return LoginResponse{}, err
	}
	if err := s.audit(ctx, &account, "auth.login", "account", account.ID, nil, nil, meta); err != nil {
		return LoginResponse{}, err
	}
	return LoginResponse{Token: token, SessionID: session.ID, Account: account, ExpiresAt: session.ExpiresAt}, nil
}

func (s Service) Authenticate(ctx context.Context, token string) (Principal, error) {
	if strings.TrimSpace(token) == "" {
		return Principal{}, errorsx.Unauthorized("missing bearer token")
	}
	session, account, err := s.repo.GetSessionByTokenHash(ctx, TokenHash(token), s.now())
	if err != nil {
		return Principal{}, err
	}
	switch account.Status {
	case AccountStatusActive:
	case AccountStatusDisabled:
		return Principal{}, errorsx.New(403, "account_disabled", "account is disabled")
	case AccountStatusDeleted:
		return Principal{}, errorsx.New(403, "account_deleted", "account is deleted")
	default:
		return Principal{}, errorsx.New(403, "account_disabled", "account is not active")
	}
	return principalFrom(session, account), nil
}

func (s Service) Logout(ctx context.Context, actor Principal, meta RequestMeta) (map[string]bool, error) {
	if _, err := s.repo.RevokeSession(ctx, actor.SessionID); err != nil {
		return nil, err
	}
	account := accountFromPrincipal(actor)
	if err := s.audit(ctx, &account, "auth.logout", "session", actor.SessionID, nil, nil, meta); err != nil {
		return nil, err
	}
	return map[string]bool{"ok": true}, nil
}

func (s Service) SessionInfo(actor Principal) Principal {
	return actor
}

func (s Service) CreateAccount(ctx context.Context, actor Principal, req AdminCreateAccountRequest, meta RequestMeta) (Account, error) {
	if err := requireAdmin(actor); err != nil {
		return Account{}, err
	}
	usernameNorm, err := validateUsername(req.Username)
	if err != nil {
		return Account{}, err
	}
	if req.Role == "" {
		req.Role = RoleUser
	}
	if err := validateRole(req.Role); err != nil {
		return Account{}, err
	}
	if req.Status == "" {
		req.Status = AccountStatusActive
	}
	if req.Status != AccountStatusActive && req.Status != AccountStatusDisabled {
		return Account{}, errorsx.BadRequest("account status must be active or disabled")
	}
	hash, algo, params, err := HashPassword(req.Password)
	if err != nil {
		return Account{}, err
	}
	account, err := s.repo.CreateAccount(ctx, AccountCreate{
		Username:       strings.TrimSpace(req.Username),
		UsernameNorm:   usernameNorm,
		DisplayName:    req.DisplayName,
		Email:          req.Email,
		Phone:          req.Phone,
		Role:           req.Role,
		Status:         req.Status,
		PasswordHash:   hash,
		PasswordAlgo:   algo,
		PasswordParams: params,
		CreatedVia:     CreatedViaAdmin,
		CreatedBy:      actor.AccountID,
	})
	if err != nil {
		return Account{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "account.create", "account", account.ID, nil, map[string]any{"username": account.Username}, meta); err != nil {
		return Account{}, err
	}
	return account, nil
}

func (s Service) ListAccounts(ctx context.Context, actor Principal, filter AccountListFilter) ([]Account, int, error) {
	if err := requireAdmin(actor); err != nil {
		return nil, 0, err
	}
	normalizePage(&filter.Limit, &filter.Offset)
	return s.repo.ListAccounts(ctx, filter)
}

func (s Service) GetAccountDetail(ctx context.Context, actor Principal, accountID string) (AccountDetail, error) {
	if err := requireAdmin(actor); err != nil {
		return AccountDetail{}, err
	}
	return s.repo.GetAccountDetail(ctx, accountID)
}

func (s Service) UpdateAccountProfile(ctx context.Context, actor Principal, accountID string, update AccountProfileUpdate, meta RequestMeta) (Account, error) {
	if err := requireAdmin(actor); err != nil {
		return Account{}, err
	}
	account, err := s.repo.UpdateAccountProfile(ctx, accountID, update)
	if err != nil {
		return Account{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "account.update", "account", account.ID, nil, map[string]any{"display_name": account.DisplayName}, meta); err != nil {
		return Account{}, err
	}
	return account, nil
}

func (s Service) SetAccountStatus(ctx context.Context, actor Principal, accountID string, status AccountStatus, meta RequestMeta) (Account, error) {
	if err := requireAdmin(actor); err != nil {
		return Account{}, err
	}
	account, affected, err := s.repo.SetAccountStatus(ctx, accountID, status)
	if err != nil {
		return Account{}, err
	}
	for _, access := range affected {
		if err := s.projectAccess(ctx, access); err != nil {
			return Account{}, err
		}
	}
	if err := s.audit(ctx, actorAccount(actor), "account.status.update", "account", account.ID, nil, map[string]any{"status": account.Status}, meta); err != nil {
		return Account{}, err
	}
	return account, nil
}

func (s Service) ResetAccountPassword(ctx context.Context, actor Principal, accountID string, req PasswordRequest, meta RequestMeta) (Account, error) {
	if err := requireAdmin(actor); err != nil {
		return Account{}, err
	}
	hash, algo, params, err := HashPassword(req.Password)
	if err != nil {
		return Account{}, err
	}
	account, err := s.repo.SetAccountPassword(ctx, accountID, hash, algo, params, req.RevokeSessions)
	if err != nil {
		return Account{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "account.password.reset", "account", account.ID, nil, map[string]any{"revoke_sessions": req.RevokeSessions}, meta); err != nil {
		return Account{}, err
	}
	return account, nil
}

func (s Service) DeleteAccount(ctx context.Context, actor Principal, accountID string, meta RequestMeta) (map[string]any, error) {
	if err := requireAdmin(actor); err != nil {
		return nil, err
	}
	account, affected, err := s.repo.SetAccountStatus(ctx, accountID, AccountStatusDeleted)
	if err != nil {
		return nil, err
	}
	for _, access := range affected {
		if err := s.projectAccess(ctx, access); err != nil {
			return nil, err
		}
	}
	if err := s.audit(ctx, actorAccount(actor), "account.delete", "account", account.ID, nil, map[string]any{"status": account.Status, "disabled_access_account_count": len(affected)}, meta); err != nil {
		return nil, err
	}
	return map[string]any{
		"account_id":                    account.ID,
		"status":                        account.Status,
		"disabled_access_account_count": len(affected),
	}, nil
}

func (s Service) ListAllAccessAccounts(ctx context.Context, actor Principal, filter AccessAccountListFilter) ([]AccessAccount, int, error) {
	if err := requireAdmin(actor); err != nil {
		return nil, 0, err
	}
	normalizePage(&filter.Limit, &filter.Offset)
	return s.repo.ListAccessAccounts(ctx, filter)
}

func (s Service) GetProfile(ctx context.Context, actor Principal) (Account, error) {
	return s.repo.GetAccount(ctx, actor.AccountID)
}

func (s Service) UpdateMyProfile(ctx context.Context, actor Principal, update AccountProfileUpdate, meta RequestMeta) (Account, error) {
	account, err := s.repo.UpdateAccountProfile(ctx, actor.AccountID, update)
	if err != nil {
		return Account{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "account.update", "account", account.ID, nil, map[string]any{"display_name": account.DisplayName}, meta); err != nil {
		return Account{}, err
	}
	return account, nil
}

func (s Service) ListMyAccessAccounts(ctx context.Context, actor Principal, filter AccessAccountListFilter) ([]AccessAccount, int, error) {
	filter.OwnerAccountID = actor.AccountID
	normalizePage(&filter.Limit, &filter.Offset)
	return s.repo.ListAccessAccounts(ctx, filter)
}

func (s Service) CreateMyAccessAccount(ctx context.Context, actor Principal, req AccessAccountCreateRequest, meta RequestMeta) (AccessAccount, error) {
	usernameNorm, err := validateUsername(req.Username)
	if err != nil {
		return AccessAccount{}, err
	}
	if req.ConcurrencyLimit < 0 {
		return AccessAccount{}, errorsx.BadRequest("concurrency_limit must be non-negative")
	}
	hash, algo, params, err := HashPassword(req.Password)
	if err != nil {
		return AccessAccount{}, err
	}
	access, err := s.repo.CreateAccessAccount(ctx, AccessAccountCreate{
		OwnerAccountID:   actor.AccountID,
		Username:         strings.TrimSpace(req.Username),
		UsernameNorm:     usernameNorm,
		DisplayName:      req.DisplayName,
		Note:             req.Note,
		PasswordHash:     hash,
		PasswordAlgo:     algo,
		PasswordParams:   params,
		ExpiresAt:        req.ExpiresAt,
		ConcurrencyLimit: req.ConcurrencyLimit,
		AuthPolicy:       req.AuthPolicy,
		CreatedBy:        actor.AccountID,
	})
	if err != nil {
		return AccessAccount{}, err
	}
	if err := s.projectAccess(ctx, access); err != nil {
		return AccessAccount{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "access_account.create", "access_account", access.ID, nil, map[string]any{"username": access.Username}, meta); err != nil {
		return AccessAccount{}, err
	}
	return access, nil
}

func (s Service) GetMyAccessAccount(ctx context.Context, actor Principal, accessAccountID string) (AccessAccount, error) {
	access, err := s.repo.GetAccessAccount(ctx, accessAccountID)
	if err != nil {
		return AccessAccount{}, err
	}
	if access.OwnerAccountID != actor.AccountID {
		return AccessAccount{}, errorsx.NotFound("access account not found")
	}
	return access, nil
}

func (s Service) UpdateMyAccessAccount(ctx context.Context, actor Principal, accessAccountID string, req AccessAccountUpdateRequest, meta RequestMeta) (AccessAccount, error) {
	if req.ConcurrencyLimit < 0 {
		return AccessAccount{}, errorsx.BadRequest("concurrency_limit must be non-negative")
	}
	access, err := s.repo.UpdateAccessAccount(ctx, accessAccountID, actor.AccountID, AccessAccountUpdate{
		DisplayName:      req.DisplayName,
		Note:             req.Note,
		ExpiresAt:        req.ExpiresAt,
		ClearExpiresAt:   req.ExpiresAt == nil,
		ConcurrencyLimit: req.ConcurrencyLimit,
		AuthPolicy:       req.AuthPolicy,
	})
	if err != nil {
		return AccessAccount{}, err
	}
	if err := s.projectAccess(ctx, access); err != nil {
		return AccessAccount{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "access_account.update", "access_account", access.ID, nil, map[string]any{"display_name": access.DisplayName}, meta); err != nil {
		return AccessAccount{}, err
	}
	return access, nil
}

func (s Service) SetMyAccessAccountPassword(ctx context.Context, actor Principal, accessAccountID string, req PasswordRequest, meta RequestMeta) (AccessAccount, error) {
	hash, algo, params, err := HashPassword(req.Password)
	if err != nil {
		return AccessAccount{}, err
	}
	access, err := s.repo.SetAccessAccountPassword(ctx, accessAccountID, actor.AccountID, hash, algo, params)
	if err != nil {
		return AccessAccount{}, err
	}
	if err := s.projectAccess(ctx, access); err != nil {
		return AccessAccount{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "access_account.password.update", "access_account", access.ID, nil, nil, meta); err != nil {
		return AccessAccount{}, err
	}
	return access, nil
}

func (s Service) SetMyAccessAccountStatus(ctx context.Context, actor Principal, accessAccountID string, status AccessStatus, meta RequestMeta) (AccessAccount, error) {
	access, err := s.repo.SetAccessAccountStatus(ctx, accessAccountID, actor.AccountID, status)
	if err != nil {
		return AccessAccount{}, err
	}
	if err := s.projectAccess(ctx, access); err != nil {
		return AccessAccount{}, err
	}
	if err := s.audit(ctx, actorAccount(actor), "access_account.status.update", "access_account", access.ID, nil, map[string]any{"status": access.Status}, meta); err != nil {
		return AccessAccount{}, err
	}
	return access, nil
}

func (s Service) DeleteMyAccessAccount(ctx context.Context, actor Principal, accessAccountID string, meta RequestMeta) (map[string]any, error) {
	access, err := s.repo.DeleteAccessAccount(ctx, accessAccountID, actor.AccountID)
	if err != nil {
		return nil, err
	}
	if err := s.projectAccess(ctx, access); err != nil {
		return nil, err
	}
	if err := s.audit(ctx, actorAccount(actor), "access_account.delete", "access_account", access.ID, nil, map[string]any{"status": access.Status}, meta); err != nil {
		return nil, err
	}
	return map[string]any{"access_account_id": access.ID, "status": access.Status}, nil
}

func (s Service) projectAccess(ctx context.Context, access AccessAccount) error {
	if err := s.projector.ProjectAccessAccount(ctx, AccessAccountAuthProjection{
		AccessAccountID:   access.ID,
		Username:          access.Username,
		UsernameNorm:      access.UsernameNorm,
		Status:            access.Status,
		OwnerAccountID:    access.OwnerAccountID,
		OwnerStatus:       access.OwnerStatus,
		PasswordHash:      access.PasswordHash,
		PasswordAlgo:      access.PasswordAlgo,
		PasswordParams:    access.PasswordParams,
		ExpiresAt:         access.ExpiresAt,
		ConcurrencyLimit:  access.ConcurrencyLimit,
		AuthPolicy:        access.AuthPolicy,
		ProjectionVersion: access.ProjectionVersion,
		UpdatedAt:         access.UpdatedAt,
	}); err != nil {
		return errorsx.New(503, "projection_unavailable", "auth projection is unavailable")
	}
	return nil
}

func (s Service) audit(ctx context.Context, actor *Account, action string, targetType string, targetID string, before map[string]any, after map[string]any, meta RequestMeta) error {
	entry := AuditEntry{
		Action:     action,
		TargetType: targetType,
		TargetID:   targetID,
		BeforeData: before,
		AfterData:  after,
		RequestID:  meta.RequestID,
		IPAddress:  meta.IPAddress,
		UserAgent:  meta.UserAgent,
	}
	if actor != nil {
		entry.ActorAccountID = actor.ID
		entry.ActorUsername = actor.Username
	}
	return s.repo.WriteAudit(ctx, entry)
}

func requireAdmin(actor Principal) error {
	if actor.Role != RoleAdmin {
		return errorsx.Forbidden("admin role is required")
	}
	return nil
}

func validateRole(role Role) error {
	switch role {
	case RoleAdmin, RoleUser:
		return nil
	default:
		return errorsx.BadRequest("role must be admin or user")
	}
}

func normalizePage(limit *int, offset *int) {
	if *limit <= 0 || *limit > 200 {
		*limit = 50
	}
	if *offset < 0 {
		*offset = 0
	}
}

func principalFrom(session Session, account Account) Principal {
	return Principal{
		SessionID:   session.ID,
		AccountID:   account.ID,
		Username:    account.Username,
		DisplayName: account.DisplayName,
		Role:        account.Role,
		Status:      account.Status,
		ExpiresAt:   session.ExpiresAt,
	}
}

func actorAccount(actor Principal) *Account {
	return &Account{
		ID:          actor.AccountID,
		Username:    actor.Username,
		DisplayName: actor.DisplayName,
		Role:        actor.Role,
		Status:      actor.Status,
	}
}

func accountFromPrincipal(actor Principal) Account {
	return Account{
		ID:          actor.AccountID,
		Username:    actor.Username,
		DisplayName: actor.DisplayName,
		Role:        actor.Role,
		Status:      actor.Status,
	}
}
