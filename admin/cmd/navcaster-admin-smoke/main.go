package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

func main() {
	baseURL := strings.TrimRight(env("NAVCASTER_ADMIN_SMOKE_URL", "http://127.0.0.1:18080"), "/")
	redisAddr := env("NAVCASTER_ADMIN_SMOKE_REDIS_ADDR", "")
	client := &http.Client{Timeout: 5 * time.Second}

	var health struct {
		Data struct {
			Postgres     string `json:"postgres"`
			Redis        string `json:"redis"`
			ControlPlane struct {
				Status     string `json:"status"`
				Repository string `json:"repository"`
			} `json:"control_plane"`
		} `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/health", nil, http.StatusOK, &health))
	if health.Data.Postgres != "connected" || health.Data.Redis != "connected" || health.Data.ControlPlane.Status != "ok" {
		die("dependency health is not ready: %#v", health.Data)
	}

	suffix := time.Now().UTC().Format("20060102150405")
	var register struct {
		Data struct {
			AgentID string `json:"agent_id"`
			HostID  string `json:"host_id"`
		} `json:"data"`
	}
	must(do(client, http.MethodPost, baseURL+"/api/v1/agents/register", map[string]any{
		"agent_id":      "ag_nc101_" + suffix,
		"host_id":       "host_nc101_" + suffix,
		"hostname":      "nc101-smoke",
		"machine_id":    "nc101-smoke",
		"os":            "smoke",
		"arch":          "amd64",
		"agent_version": "nc101",
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
		"name":              "nc101-smoke-runtime",
		"config_version":    101,
		"listen_port":       4202,
		"worker_count":      1,
		"max_worker_count":  2,
		"restart_policy":    "on_failure",
		"start_immediately": false,
	}, http.StatusCreated, &created))
	requireRedisJSON(redisAddr, "v2:config:runtime:"+created.Data.RuntimeID, "runtime_id", created.Data.RuntimeID)
	requireRedisJSON(redisAddr, "v2:control:desired-state:"+register.Data.HostID, "host_id", register.Data.HostID)

	var action struct {
		Data struct {
			IntentID       string `json:"intent_id"`
			Status         string `json:"status"`
			DesiredVersion int64  `json:"desired_version"`
			Runtime        struct {
				Control struct {
					Status string `json:"status"`
				} `json:"control"`
			} `json:"runtime"`
		} `json:"data"`
	}
	must(do(client, http.MethodPost, baseURL+"/api/v1/control/runtimes/"+created.Data.RuntimeID+"/actions/start", map[string]any{
		"request_id": "nc101-smoke-" + suffix,
	}, http.StatusAccepted, &action))
	if action.Data.IntentID == "" || action.Data.Status != "projected" {
		die("action intent was not projected: %#v", action.Data)
	}
	requireRedisJSON(redisAddr, "v2:control:intent:"+action.Data.IntentID, "intent_id", action.Data.IntentID)

	must(do(client, http.MethodPost, baseURL+"/api/v1/agents/"+register.Data.AgentID+"/runtime-metrics", map[string]any{
		"agent_id": register.Data.AgentID,
		"host_id":  register.Data.HostID,
		"actual": []map[string]any{{
			"runtime_id":               created.Data.RuntimeID,
			"actual_state":             "running",
			"process_id":               101,
			"config_version":           101,
			"listen_port":              4202,
			"worker_count":             1,
			"redis_connected":          true,
			"observed_desired_version": action.Data.DesiredVersion,
			"updated_at":               time.Now().UTC().Format(time.RFC3339Nano),
		}},
	}, http.StatusAccepted, nil))
	requireRedisJSON(redisAddr, "v2:runtime:actual:"+created.Data.RuntimeID, "status", "observed")

	must(do(client, http.MethodPost, baseURL+"/api/v1/agents/"+register.Data.AgentID+"/runtime-events", map[string]any{
		"agent_id": register.Data.AgentID,
		"host_id":  register.Data.HostID,
		"events": []map[string]any{{
			"event_id":        "evt_nc101_" + suffix,
			"runtime_id":      created.Data.RuntimeID,
			"type":            "runtime_converged",
			"severity":        "info",
			"desired_version": action.Data.DesiredVersion,
			"message":         "nc101 smoke converged",
			"occurred_at":     time.Now().UTC().Format(time.RFC3339Nano),
		}},
	}, http.StatusAccepted, nil))

	var runtime struct {
		Data struct {
			Control struct {
				Status string `json:"status"`
				Reason string `json:"reason"`
			} `json:"control"`
		} `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/control/runtimes/"+created.Data.RuntimeID, nil, http.StatusOK, &runtime))
	if runtime.Data.Control.Status != "converged" {
		die("runtime did not converge: %#v", runtime.Data.Control)
	}

	var intents struct {
		Data []struct {
			IntentID string `json:"intent_id"`
			Status   string `json:"status"`
		} `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/control/runtimes/"+created.Data.RuntimeID+"/intents", nil, http.StatusOK, &intents))
	if len(intents.Data) == 0 || intents.Data[0].Status != "observed" {
		die("intent did not become observed: %#v", intents.Data)
	}

	var events struct {
		Data []struct {
			EventID   string `json:"event_id"`
			RuntimeID string `json:"runtime_id"`
			Type      string `json:"type"`
		} `json:"data"`
	}
	must(do(client, http.MethodGet, baseURL+"/api/v1/control/runtimes/"+created.Data.RuntimeID+"/events", nil, http.StatusOK, &events))
	if len(events.Data) == 0 || events.Data[0].Type != "runtime_converged" {
		die("runtime event was not queryable: %#v", events.Data)
	}

	fmt.Printf("NC-101 admin control smoke ok: host=%s agent=%s runtime=%s intent=%s desired_version=%d\n",
		register.Data.HostID, register.Data.AgentID, created.Data.RuntimeID, action.Data.IntentID, action.Data.DesiredVersion)
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

func requireRedisJSON(addr string, key string, field string, want string) {
	if addr == "" {
		die("NAVCASTER_ADMIN_SMOKE_REDIS_ADDR is required for projection checks")
	}
	raw, err := redisDo(addr, "GET", key)
	if err != nil {
		die("redis GET %s failed: %v", key, err)
	}
	if raw == "" {
		die("redis key %s is empty", key)
	}
	var value map[string]any
	if err := json.Unmarshal([]byte(raw), &value); err != nil {
		die("redis key %s is not JSON: %v payload=%s", key, err, raw)
	}
	if field == "intent_id" {
		if intent, ok := value["intent"].(map[string]any); ok {
			value = intent
		}
	}
	if field == "runtime_id" {
		if _, ok := value[field]; !ok {
			if runtime, ok := value["runtime"].(map[string]any); ok {
				value = runtime
			}
		}
	}
	got := fmt.Sprint(value[field])
	if got != want {
		die("redis key %s field %s = %s, want %s payload=%s", key, field, got, want, raw)
	}
}

func redisDo(addr string, args ...string) (string, error) {
	conn, err := net.DialTimeout("tcp", addr, 3*time.Second)
	if err != nil {
		return "", err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(3 * time.Second))
	if _, err := conn.Write(encodeRESP(args)); err != nil {
		return "", err
	}
	return readRESP(bufio.NewReader(conn))
}

func encodeRESP(args []string) []byte {
	var b strings.Builder
	b.WriteString("*")
	b.WriteString(strconv.Itoa(len(args)))
	b.WriteString("\r\n")
	for _, arg := range args {
		b.WriteString("$")
		b.WriteString(strconv.Itoa(len(arg)))
		b.WriteString("\r\n")
		b.WriteString(arg)
		b.WriteString("\r\n")
	}
	return []byte(b.String())
}

func readRESP(reader *bufio.Reader) (string, error) {
	prefix, err := reader.ReadByte()
	if err != nil {
		return "", err
	}
	line, err := reader.ReadString('\n')
	if err != nil {
		return "", err
	}
	line = strings.TrimSuffix(strings.TrimSuffix(line, "\n"), "\r")
	switch prefix {
	case '+', ':':
		return line, nil
	case '-':
		return "", fmt.Errorf("redis error: %s", line)
	case '$':
		n, err := strconv.Atoi(line)
		if err != nil {
			return "", err
		}
		if n < 0 {
			return "", nil
		}
		buf := make([]byte, n+2)
		if _, err := io.ReadFull(reader, buf); err != nil {
			return "", err
		}
		return string(buf[:n]), nil
	default:
		return "", fmt.Errorf("unsupported redis response prefix %q", prefix)
	}
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
