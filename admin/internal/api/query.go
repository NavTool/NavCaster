package api

import (
	"net/http"
	"strconv"

	"navcaster-admin/internal/errorsx"
)

func parseInt64Query(r *http.Request, name string, fallback int64) (int64, error) {
	value := r.URL.Query().Get(name)
	if value == "" {
		return fallback, nil
	}
	parsed, err := strconv.ParseInt(value, 10, 64)
	if err != nil || parsed < 0 {
		return 0, errorsx.BadRequest(name + " must be a non-negative integer")
	}
	return parsed, nil
}
