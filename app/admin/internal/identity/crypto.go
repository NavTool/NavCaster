package identity

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"math"
	"strings"
	"time"

	"navcaster-admin/internal/errorsx"
)

const (
	passwordAlgo       = "pbkdf2-sha256"
	passwordIterations = 120000
	passwordKeyLength  = 32
)

func HashPassword(password string) (string, string, map[string]any, error) {
	if err := validatePassword(password); err != nil {
		return "", "", nil, err
	}
	salt := randomBytes(16)
	key := pbkdf2SHA256([]byte(password), salt, passwordIterations, passwordKeyLength)
	params := map[string]any{
		"iterations": passwordIterations,
		"salt":       base64.RawStdEncoding.EncodeToString(salt),
	}
	return base64.RawStdEncoding.EncodeToString(key), passwordAlgo, params, nil
}

func VerifyPassword(password string, hash string, algo string, params map[string]any) bool {
	if algo != passwordAlgo || hash == "" {
		return false
	}
	iterations, ok := intParam(params["iterations"])
	if !ok || iterations <= 0 || iterations > 1000000 {
		return false
	}
	saltText, _ := params["salt"].(string)
	salt, err := base64.RawStdEncoding.DecodeString(saltText)
	if err != nil || len(salt) == 0 {
		return false
	}
	expected, err := base64.RawStdEncoding.DecodeString(hash)
	if err != nil || len(expected) == 0 {
		return false
	}
	actual := pbkdf2SHA256([]byte(password), salt, iterations, len(expected))
	return subtle.ConstantTimeCompare(actual, expected) == 1
}

func NewID(prefix string) string {
	return prefix + "_" + randomHex(10)
}

func NewToken() string {
	return base64.RawURLEncoding.EncodeToString(randomBytes(32))
}

func TokenHash(token string) string {
	sum := sha256.Sum256([]byte(token))
	return hex.EncodeToString(sum[:])
}

func NormalizeUsername(username string) string {
	return strings.ToLower(strings.TrimSpace(username))
}

func validateUsername(username string) (string, error) {
	norm := NormalizeUsername(username)
	if len(norm) < 3 || len(norm) > 64 {
		return "", errorsx.BadRequest("username must be 3..64 characters")
	}
	for _, r := range norm {
		switch {
		case r >= 'a' && r <= 'z':
		case r >= '0' && r <= '9':
		case r == '-', r == '_', r == '.', r == '@':
		default:
			return "", errorsx.BadRequest("username contains unsupported characters")
		}
	}
	return norm, nil
}

func validatePassword(password string) error {
	if len(password) < 8 {
		return errorsx.BadRequest("password must be at least 8 characters")
	}
	if len(password) > 256 {
		return errorsx.BadRequest("password is too long")
	}
	return nil
}

func randomHex(bytesLen int) string {
	return hex.EncodeToString(randomBytes(bytesLen))
}

func randomBytes(n int) []byte {
	buf := make([]byte, n)
	if _, err := rand.Read(buf); err != nil {
		fallback := fmt.Sprintf("%d", time.Now().UTC().UnixNano())
		sum := sha256.Sum256([]byte(fallback))
		return sum[:n]
	}
	return buf
}

func intParam(value any) (int, bool) {
	switch v := value.(type) {
	case int:
		return v, true
	case int64:
		return int(v), true
	case float64:
		if math.Trunc(v) == v {
			return int(v), true
		}
	}
	return 0, false
}

func pbkdf2SHA256(password []byte, salt []byte, iterations int, keyLength int) []byte {
	hashLen := sha256.Size
	blocks := (keyLength + hashLen - 1) / hashLen
	out := make([]byte, 0, blocks*hashLen)
	for block := 1; block <= blocks; block++ {
		mac := hmac.New(sha256.New, password)
		mac.Write(salt)
		mac.Write([]byte{byte(block >> 24), byte(block >> 16), byte(block >> 8), byte(block)})
		u := mac.Sum(nil)
		t := append([]byte(nil), u...)
		for i := 1; i < iterations; i++ {
			mac = hmac.New(sha256.New, password)
			mac.Write(u)
			u = mac.Sum(nil)
			for j := range t {
				t[j] ^= u[j]
			}
		}
		out = append(out, t...)
	}
	return out[:keyLength]
}
