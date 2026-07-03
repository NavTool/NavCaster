package identity

import (
	"context"
	"time"
)

type Role string

const (
	RoleAdmin Role = "admin"
	RoleUser  Role = "user"
)

type AccountStatus string

const (
	AccountStatusActive              AccountStatus = "active"
	AccountStatusDisabled            AccountStatus = "disabled"
	AccountStatusDeleted             AccountStatus = "deleted"
	AccountStatusPendingVerification AccountStatus = "pending_verification"
)

type CreatedVia string

const (
	CreatedViaAdmin       CreatedVia = "admin"
	CreatedViaSelfService CreatedVia = "self_service"
)

type AccessStatus string

const (
	AccessStatusActive   AccessStatus = "active"
	AccessStatusDisabled AccessStatus = "disabled"
	AccessStatusDeleted  AccessStatus = "deleted"
)

type SessionStatus string

const (
	SessionStatusActive  SessionStatus = "active"
	SessionStatusRevoked SessionStatus = "revoked"
	SessionStatusExpired SessionStatus = "expired"
)

type Account struct {
	ID             string                 `json:"account_id"`
	Username       string                 `json:"username"`
	UsernameNorm   string                 `json:"username_norm,omitempty"`
	DisplayName    string                 `json:"display_name"`
	Email          string                 `json:"email,omitempty"`
	Phone          string                 `json:"phone,omitempty"`
	Role           Role                   `json:"role"`
	Status         AccountStatus          `json:"status"`
	PasswordHash   string                 `json:"-"`
	PasswordAlgo   string                 `json:"-"`
	PasswordParams map[string]any         `json:"-"`
	CreatedVia     CreatedVia             `json:"created_via"`
	CreatedBy      string                 `json:"created_by,omitempty"`
	CreatedAt      time.Time              `json:"created_at"`
	UpdatedAt      time.Time              `json:"updated_at"`
	DisabledAt     *time.Time             `json:"disabled_at,omitempty"`
	DeletedAt      *time.Time             `json:"deleted_at,omitempty"`
	Extra          map[string]interface{} `json:"-"`
}

type AccountDetail struct {
	Account
	AccessAccountCount int `json:"access_account_count"`
	ActiveSessionCount int `json:"active_session_count"`
}

type AccessAccount struct {
	ID                string         `json:"access_account_id"`
	OwnerAccountID    string         `json:"owner_account_id"`
	OwnerUsername     string         `json:"owner_username,omitempty"`
	OwnerStatus       AccountStatus  `json:"owner_status,omitempty"`
	Username          string         `json:"username"`
	UsernameNorm      string         `json:"username_norm,omitempty"`
	DisplayName       string         `json:"display_name"`
	Note              string         `json:"note,omitempty"`
	Status            AccessStatus   `json:"status"`
	PasswordHash      string         `json:"-"`
	PasswordAlgo      string         `json:"-"`
	PasswordParams    map[string]any `json:"-"`
	ExpiresAt         *time.Time     `json:"expires_at,omitempty"`
	ConcurrencyLimit  int            `json:"concurrency_limit"`
	AuthPolicy        map[string]any `json:"auth_policy,omitempty"`
	ProjectionVersion int64          `json:"projection_version"`
	CreatedBy         string         `json:"created_by,omitempty"`
	CreatedAt         time.Time      `json:"created_at"`
	UpdatedAt         time.Time      `json:"updated_at"`
	DisabledAt        *time.Time     `json:"disabled_at,omitempty"`
	DeletedAt         *time.Time     `json:"deleted_at,omitempty"`
}

type Session struct {
	ID         string        `json:"session_id"`
	AccountID  string        `json:"account_id"`
	TokenHash  string        `json:"-"`
	Status     SessionStatus `json:"status"`
	IssuedAt   time.Time     `json:"issued_at"`
	ExpiresAt  time.Time     `json:"expires_at"`
	RevokedAt  *time.Time    `json:"revoked_at,omitempty"`
	LastSeenAt *time.Time    `json:"last_seen_at,omitempty"`
	UserAgent  string        `json:"user_agent,omitempty"`
	IPAddress  string        `json:"ip_address,omitempty"`
}

type Principal struct {
	SessionID   string        `json:"session_id"`
	AccountID   string        `json:"account_id"`
	Username    string        `json:"username"`
	DisplayName string        `json:"display_name"`
	Role        Role          `json:"role"`
	Status      AccountStatus `json:"status"`
	ExpiresAt   time.Time     `json:"expires_at"`
}

type RequestMeta struct {
	RequestID string
	IPAddress string
	UserAgent string
}

type AuditEntry struct {
	ActorAccountID string
	ActorUsername  string
	Action         string
	TargetType     string
	TargetID       string
	BeforeData     map[string]any
	AfterData      map[string]any
	RequestID      string
	IPAddress      string
	UserAgent      string
}

type AccountCreate struct {
	Username       string
	UsernameNorm   string
	DisplayName    string
	Email          string
	Phone          string
	Role           Role
	Status         AccountStatus
	PasswordHash   string
	PasswordAlgo   string
	PasswordParams map[string]any
	CreatedVia     CreatedVia
	CreatedBy      string
}

type AccountProfileUpdate struct {
	DisplayName string
	Email       string
	Phone       string
}

type AccountListFilter struct {
	Status AccountStatus
	Role   Role
	Search string
	Limit  int
	Offset int
}

type AccessAccountCreate struct {
	OwnerAccountID   string
	Username         string
	UsernameNorm     string
	DisplayName      string
	Note             string
	PasswordHash     string
	PasswordAlgo     string
	PasswordParams   map[string]any
	ExpiresAt        *time.Time
	ConcurrencyLimit int
	AuthPolicy       map[string]any
	CreatedBy        string
}

type AccessAccountUpdate struct {
	DisplayName      string
	Note             string
	ExpiresAt        *time.Time
	ClearExpiresAt   bool
	ConcurrencyLimit int
	AuthPolicy       map[string]any
}

type AccessAccountListFilter struct {
	OwnerAccountID string
	Status         AccessStatus
	Search         string
	Limit          int
	Offset         int
}

type AccessAccountAuthProjection struct {
	AccessAccountID   string         `json:"access_account_id"`
	Username          string         `json:"username"`
	UsernameNorm      string         `json:"username_norm"`
	Status            AccessStatus   `json:"status"`
	OwnerAccountID    string         `json:"owner_account_id"`
	OwnerStatus       AccountStatus  `json:"owner_status"`
	PasswordHash      string         `json:"password_hash"`
	PasswordAlgo      string         `json:"password_algo"`
	PasswordParams    map[string]any `json:"password_params"`
	ExpiresAt         *time.Time     `json:"expires_at"`
	ConcurrencyLimit  int            `json:"concurrency_limit"`
	AuthPolicy        map[string]any `json:"auth_policy"`
	ProjectionVersion int64          `json:"projection_version"`
	UpdatedAt         time.Time      `json:"updated_at"`
}

type Repository interface {
	CreateAccount(ctx context.Context, account AccountCreate) (Account, error)
	GetAccount(ctx context.Context, accountID string) (Account, error)
	GetAccountByUsernameNorm(ctx context.Context, usernameNorm string) (Account, error)
	GetAccountDetail(ctx context.Context, accountID string) (AccountDetail, error)
	ListAccounts(ctx context.Context, filter AccountListFilter) ([]Account, int, error)
	UpdateAccountProfile(ctx context.Context, accountID string, update AccountProfileUpdate) (Account, error)
	SetAccountStatus(ctx context.Context, accountID string, status AccountStatus) (Account, []AccessAccount, error)
	SetAccountPassword(ctx context.Context, accountID string, passwordHash string, passwordAlgo string, passwordParams map[string]any, revokeSessions bool) (Account, error)
	RevokeSessionsForAccount(ctx context.Context, accountID string) (int, error)

	CreateSession(ctx context.Context, session Session) (Session, error)
	GetSessionByTokenHash(ctx context.Context, tokenHash string, now time.Time) (Session, Account, error)
	RevokeSession(ctx context.Context, sessionID string) (Session, error)

	CreateAccessAccount(ctx context.Context, account AccessAccountCreate) (AccessAccount, error)
	GetAccessAccount(ctx context.Context, accessAccountID string) (AccessAccount, error)
	ListAccessAccounts(ctx context.Context, filter AccessAccountListFilter) ([]AccessAccount, int, error)
	UpdateAccessAccount(ctx context.Context, accessAccountID string, ownerAccountID string, update AccessAccountUpdate) (AccessAccount, error)
	SetAccessAccountPassword(ctx context.Context, accessAccountID string, ownerAccountID string, passwordHash string, passwordAlgo string, passwordParams map[string]any) (AccessAccount, error)
	SetAccessAccountStatus(ctx context.Context, accessAccountID string, ownerAccountID string, status AccessStatus) (AccessAccount, error)
	DeleteAccessAccount(ctx context.Context, accessAccountID string, ownerAccountID string) (AccessAccount, error)

	WriteAudit(ctx context.Context, entry AuditEntry) error
}

type AuthProjector interface {
	ProjectAccessAccount(ctx context.Context, projection AccessAccountAuthProjection) error
}

type NoopProjector struct{}

func (NoopProjector) ProjectAccessAccount(context.Context, AccessAccountAuthProjection) error {
	return nil
}
