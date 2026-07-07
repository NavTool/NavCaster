package api

import (
	"net"
	"net/http"
	"strings"

	"navcaster-admin/internal/errorsx"
	"navcaster-admin/internal/identity"
)

func (s Server) registerUser(w http.ResponseWriter, r *http.Request) {
	var req identity.RegisterRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.Register(r.Context(), req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusCreated, account)
}

func (s Server) login(w http.ResponseWriter, r *http.Request) {
	var req identity.LoginRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.identity.Login(r.Context(), req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, result)
}

func (s Server) logout(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.identity.Logout(r.Context(), actor, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, result)
}

func (s Server) sessionInfo(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, s.identity.SessionInfo(actor))
}

func (s Server) listAccounts(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	filter := identity.AccountListFilter{
		Status: identity.AccountStatus(r.URL.Query().Get("status")),
		Role:   identity.Role(r.URL.Query().Get("role")),
		Search: r.URL.Query().Get("search"),
	}
	filter.Limit, err = parseIntQuery(r, "limit", 50)
	if err != nil {
		writeError(w, r, err)
		return
	}
	filter.Offset, err = parseIntQuery(r, "offset", 0)
	if err != nil {
		writeError(w, r, err)
		return
	}
	accounts, total, err := s.identity.ListAccounts(r.Context(), actor, filter)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writePageData(w, r, http.StatusOK, accounts, filter.Limit, filter.Offset, total)
}

func (s Server) createAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AdminCreateAccountRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.CreateAccount(r.Context(), actor, req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusCreated, account)
}

func (s Server) getAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.GetAccountDetail(r.Context(), actor, r.PathValue("account_id"))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, account)
}

func (s Server) updateAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AccountProfileUpdate
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.UpdateAccountProfile(r.Context(), actor, r.PathValue("account_id"), req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, account)
}

func (s Server) updateAccountStatus(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AccountStatusRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.SetAccountStatus(r.Context(), actor, r.PathValue("account_id"), req.Status, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, account)
}

func (s Server) resetAccountPassword(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.PasswordRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.ResetAccountPassword(r.Context(), actor, r.PathValue("account_id"), req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, account)
}

func (s Server) deleteAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.identity.DeleteAccount(r.Context(), actor, r.PathValue("account_id"), requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, result)
}

func (s Server) listAdminAccessAccounts(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	filter, err := accessAccountFilterFrom(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	filter.OwnerAccountID = r.URL.Query().Get("owner_account_id")
	accounts, total, err := s.identity.ListAllAccessAccounts(r.Context(), actor, filter)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writePageData(w, r, http.StatusOK, accounts, filter.Limit, filter.Offset, total)
}

func (s Server) getProfile(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.GetProfile(r.Context(), actor)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, account)
}

func (s Server) updateMyProfile(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AccountProfileUpdate
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	account, err := s.identity.UpdateMyProfile(r.Context(), actor, req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, account)
}

func (s Server) listMyAccessAccounts(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	filter, err := accessAccountFilterFrom(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	accounts, total, err := s.identity.ListMyAccessAccounts(r.Context(), actor, filter)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writePageData(w, r, http.StatusOK, accounts, filter.Limit, filter.Offset, total)
}

func (s Server) createMyAccessAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AccessAccountCreateRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	access, err := s.identity.CreateMyAccessAccount(r.Context(), actor, req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusCreated, access)
}

func (s Server) getMyAccessAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	access, err := s.identity.GetMyAccessAccount(r.Context(), actor, r.PathValue("access_account_id"))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, access)
}

func (s Server) updateMyAccessAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AccessAccountUpdateRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	access, err := s.identity.UpdateMyAccessAccount(r.Context(), actor, r.PathValue("access_account_id"), req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, access)
}

func (s Server) updateMyAccessAccountPassword(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.PasswordRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	access, err := s.identity.SetMyAccessAccountPassword(r.Context(), actor, r.PathValue("access_account_id"), req, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, access)
}

func (s Server) updateMyAccessAccountStatus(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	var req identity.AccessAccountStatusRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	access, err := s.identity.SetMyAccessAccountStatus(r.Context(), actor, r.PathValue("access_account_id"), req.Status, requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, access)
}

func (s Server) deleteMyAccessAccount(w http.ResponseWriter, r *http.Request) {
	actor, err := s.requireSession(r)
	if err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.identity.DeleteMyAccessAccount(r.Context(), actor, r.PathValue("access_account_id"), requestMeta(r))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, result)
}

func (s Server) requireSession(r *http.Request) (identity.Principal, error) {
	token := bearerToken(r)
	if token == "" {
		return identity.Principal{}, errorsx.Unauthorized("missing bearer token")
	}
	return s.identity.Authenticate(r.Context(), token)
}

func bearerToken(r *http.Request) string {
	header := strings.TrimSpace(r.Header.Get("Authorization"))
	if header == "" {
		return ""
	}
	kind, token, ok := strings.Cut(header, " ")
	if !ok || !strings.EqualFold(kind, "Bearer") {
		return ""
	}
	return strings.TrimSpace(token)
}

func requestMeta(r *http.Request) identity.RequestMeta {
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		ip = r.RemoteAddr
	}
	if forwarded := strings.TrimSpace(r.Header.Get("X-Forwarded-For")); forwarded != "" {
		ip, _, _ = strings.Cut(forwarded, ",")
		ip = strings.TrimSpace(ip)
	}
	return identity.RequestMeta{
		RequestID: requestIDFrom(r),
		IPAddress: ip,
		UserAgent: r.UserAgent(),
	}
}

func accessAccountFilterFrom(r *http.Request) (identity.AccessAccountListFilter, error) {
	limit, err := parseIntQuery(r, "limit", 50)
	if err != nil {
		return identity.AccessAccountListFilter{}, err
	}
	offset, err := parseIntQuery(r, "offset", 0)
	if err != nil {
		return identity.AccessAccountListFilter{}, err
	}
	return identity.AccessAccountListFilter{
		Status: identity.AccessStatus(r.URL.Query().Get("status")),
		Search: r.URL.Query().Get("search"),
		Limit:  limit,
		Offset: offset,
	}, nil
}
