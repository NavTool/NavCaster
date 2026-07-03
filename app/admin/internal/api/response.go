package api

import (
	"encoding/json"
	"net/http"

	"navcaster-admin/internal/errorsx"
)

type responseEnvelope struct {
	RequestID string         `json:"request_id"`
	Data      any            `json:"data,omitempty"`
	Page      *pageEnvelope  `json:"page,omitempty"`
	Meta      map[string]any `json:"meta,omitempty"`
	Error     *errorEnvelope `json:"error,omitempty"`
}

type pageEnvelope struct {
	Limit  int `json:"limit"`
	Offset int `json:"offset"`
	Total  int `json:"total"`
}

type errorEnvelope struct {
	Code    string         `json:"code"`
	Message string         `json:"message"`
	Details map[string]any `json:"details,omitempty"`
}

func writeData(w http.ResponseWriter, r *http.Request, status int, data any) {
	writeJSON(w, status, responseEnvelope{
		RequestID: requestIDFrom(r),
		Data:      data,
	})
}

func writePageData(w http.ResponseWriter, r *http.Request, status int, data any, limit int, offset int, total int) {
	writeJSON(w, status, responseEnvelope{
		RequestID: requestIDFrom(r),
		Data:      data,
		Page:      &pageEnvelope{Limit: limit, Offset: offset, Total: total},
	})
}

func writeError(w http.ResponseWriter, r *http.Request, err error) {
	appErr := errorsx.From(err)
	writeJSON(w, appErr.Status, responseEnvelope{
		RequestID: requestIDFrom(r),
		Error: &errorEnvelope{
			Code:    appErr.Code,
			Message: appErr.Message,
			Details: appErr.Details,
		},
	})
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(payload)
}

func decodeJSON(r *http.Request, dst any) error {
	defer r.Body.Close()
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(dst); err != nil {
		return errorsx.BadRequest("invalid JSON body")
	}
	return nil
}
