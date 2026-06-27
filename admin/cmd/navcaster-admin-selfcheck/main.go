package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"
)

func main() {
	baseURL := strings.TrimRight(env("NAVCASTER_ADMIN_SELF_CHECK_URL", "http://127.0.0.1:8080"), "/")
	client := &http.Client{Timeout: 5 * time.Second}

	var health struct {
		Data map[string]any `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/health", nil, http.StatusOK, &health))
	if health.Data["status"] != "ok" {
		die("health status is not ok: %#v", health.Data)
	}

	hostName := "selfcheck-" + time.Now().UTC().Format("20060102150405")
	var register struct {
		Data struct {
			AgentID string `json:"agent_id"`
			HostID  string `json:"host_id"`
		} `json:"data"`
	}
	must(do(client, http.MethodPost, baseURL+"/api/v1/agents/register", map[string]any{
		"hostname":      hostName,
		"machine_id":    hostName,
		"os":            "selfcheck",
		"arch":          "amd64",
		"agent_version": "selfcheck",
	}, http.StatusCreated, &register))

	var created struct {
		Data struct {
			RuntimeID string `json:"runtime_id"`
			Desired   struct {
				Version int64 `json:"version"`
			} `json:"desired"`
		} `json:"data"`
	}
	must(do(client, http.MethodPost, baseURL+"/api/v1/control/runtimes", map[string]any{
		"host_id":           register.Data.HostID,
		"name":              "selfcheck-runtime",
		"config_version":    1,
		"listen_port":       4202,
		"worker_count":      1,
		"max_worker_count":  2,
		"restart_policy":    "on_failure",
		"start_immediately": true,
	}, http.StatusCreated, &created))

	var desired struct {
		Data struct {
			Version  int64 `json:"version"`
			Runtimes []struct {
				RuntimeID string `json:"runtime_id"`
			} `json:"runtimes"`
		} `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/agents/"+register.Data.AgentID+"/desired-state?since_version=0", nil, http.StatusOK, &desired))
	if desired.Data.Version == 0 || len(desired.Data.Runtimes) == 0 {
		die("desired-state did not include created runtime: %#v", desired.Data)
	}

	must(do(client, http.MethodPost, baseURL+"/api/v1/agents/"+register.Data.AgentID+"/runtime-metrics", map[string]any{
		"agent_id": register.Data.AgentID,
		"host_id":  register.Data.HostID,
		"actual": []map[string]any{{
			"runtime_id":               created.Data.RuntimeID,
			"actual_state":             "running",
			"process_id":               4242,
			"observed_desired_version": created.Data.Desired.Version,
			"updated_at":               time.Now().UTC().Format(time.RFC3339Nano),
		}},
	}, http.StatusAccepted, nil))

	var runtime struct {
		Data struct {
			RuntimeID string `json:"runtime_id"`
			Actual    struct {
				ActualState string `json:"actual_state"`
				ProcessID   int    `json:"process_id"`
			} `json:"actual"`
		} `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/control/runtimes/"+created.Data.RuntimeID, nil, http.StatusOK, &runtime))
	if runtime.Data.Actual.ActualState != "running" || runtime.Data.Actual.ProcessID != 4242 {
		die("runtime actual was not returned by control API: %#v", runtime.Data.Actual)
	}

	fmt.Printf("navcaster-admin self-check ok: host=%s agent=%s runtime=%s desired_version=%d\n",
		register.Data.HostID, register.Data.AgentID, created.Data.RuntimeID, desired.Data.Version)
}

func do(client *http.Client, method string, url string, body any, wantStatus int, dst any) error {
	var reader io.Reader
	if body != nil {
		payload, err := json.Marshal(body)
		if err != nil {
			return err
		}
		reader = bytes.NewReader(payload)
	}
	req, err := http.NewRequest(method, url, reader)
	if err != nil {
		return err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	respBody, _ := io.ReadAll(resp.Body)
	if resp.StatusCode != wantStatus {
		return fmt.Errorf("%s %s returned %d, want %d: %s", method, url, resp.StatusCode, wantStatus, string(respBody))
	}
	if dst != nil {
		if err := json.Unmarshal(respBody, dst); err != nil {
			return fmt.Errorf("decode %s %s: %w body=%s", method, url, err, string(respBody))
		}
	}
	return nil
}

func must(err error) {
	if err != nil {
		die("%v", err)
	}
}

func die(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}

func env(name string, fallback string) string {
	value := os.Getenv(name)
	if value == "" {
		return fallback
	}
	return value
}
