package runtime

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type ConfigRenderer struct {
	Root string
}

func NewConfigRenderer(root string) ConfigRenderer {
	return ConfigRenderer{Root: root}
}

func (r ConfigRenderer) Render(ctx context.Context, desired DesiredState) (string, string, error) {
	if err := ctx.Err(); err != nil {
		return "", "", err
	}
	if desired.RuntimeID == "" {
		return "", "", fmt.Errorf("runtime_id is required")
	}
	root := r.Root
	if strings.TrimSpace(root) == "" {
		root = "runtime"
	}

	runtimeDir := filepath.Join(root, "runtimes", safeRuntimeID(desired.RuntimeID))
	if err := os.MkdirAll(runtimeDir, 0o755); err != nil {
		return "", "", fmt.Errorf("create runtime config directory: %w", err)
	}

	payload := struct {
		RuntimeID      string            `json:"runtime_id"`
		ConfigVersion  int               `json:"config_version"`
		ListenPort     int               `json:"listen_port,omitempty"`
		WorkerCount    int               `json:"worker_count,omitempty"`
		MaxWorkerCount int               `json:"max_worker_count,omitempty"`
		RestartPolicy  string            `json:"restart_policy,omitempty"`
		Draining       bool              `json:"draining,omitempty"`
		Env            map[string]string `json:"env,omitempty"`
	}{
		RuntimeID:      desired.RuntimeID,
		ConfigVersion:  desired.ConfigVersion,
		ListenPort:     desired.ListenPort,
		WorkerCount:    desired.WorkerCount,
		MaxWorkerCount: desired.MaxWorkerCount,
		RestartPolicy:  string(desired.NormalizedRestartPolicy()),
		Draining:       desired.Draining,
		Env:            desired.Env,
	}

	data, err := json.MarshalIndent(payload, "", "  ")
	if err != nil {
		return "", "", fmt.Errorf("encode runtime config: %w", err)
	}
	sum := sha256.Sum256(data)
	checksum := hex.EncodeToString(sum[:])

	path := filepath.Join(runtimeDir, "config.json")
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return "", "", fmt.Errorf("write runtime config temp file: %w", err)
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return "", "", fmt.Errorf("replace runtime config: %w", err)
	}
	return path, checksum, nil
}

func safeRuntimeID(runtimeID string) string {
	var b strings.Builder
	for _, r := range runtimeID {
		switch {
		case r >= 'a' && r <= 'z':
			b.WriteRune(r)
		case r >= 'A' && r <= 'Z':
			b.WriteRune(r)
		case r >= '0' && r <= '9':
			b.WriteRune(r)
		case r == '-', r == '_', r == '.':
			b.WriteRune(r)
		default:
			b.WriteByte('_')
		}
	}
	if b.Len() == 0 {
		return "runtime"
	}
	return b.String()
}
