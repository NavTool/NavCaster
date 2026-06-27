package api

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"navcaster-admin/internal/agent"
	"navcaster-admin/internal/config"
	"navcaster-admin/internal/control"
	"navcaster-admin/internal/projection"
	redisStore "navcaster-admin/internal/storage/redis"
)

func TestHealthEnvelope(t *testing.T) {
	server := newTestServer()
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/health", nil)
	server.Handler().ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d body = %s", rec.Code, rec.Body.String())
	}
	var envelope map[string]any
	decodeBody(t, rec, &envelope)
	if envelope["request_id"] == "" {
		t.Fatalf("missing request_id: %#v", envelope)
	}
	data := envelope["data"].(map[string]any)
	if data["service"] != "navcaster-admin" || data["status"] != "ok" {
		t.Fatalf("unexpected health data: %#v", data)
	}
	if data["postgres"] != "not_configured" || data["redis"] != "not_configured" {
		t.Fatalf("unexpected dependency status: %#v", data)
	}
	controlPlane := data["control_plane"].(map[string]any)
	if controlPlane["status"] != "ok" || controlPlane["repository"] != "memory" {
		t.Fatalf("unexpected control plane status: %#v", controlPlane)
	}
}

func TestAgentRegisterHeartbeatDesiredState(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()

	registerBody := `{"hostname":"caster-node-01","machine_id":"machine-1","os":"windows","arch":"amd64","agent_version":"v2.0.0"}`
	registerRec := doJSON(handler, http.MethodPost, "/api/v1/agents/register", registerBody)
	if registerRec.Code != http.StatusCreated {
		t.Fatalf("register status = %d body = %s", registerRec.Code, registerRec.Body.String())
	}
	var registerEnvelope struct {
		Data struct {
			AgentID string `json:"agent_id"`
			HostID  string `json:"host_id"`
		} `json:"data"`
	}
	decodeBody(t, registerRec, &registerEnvelope)
	if registerEnvelope.Data.AgentID == "" || registerEnvelope.Data.HostID == "" {
		t.Fatalf("missing register IDs: %#v", registerEnvelope.Data)
	}

	runtimeBody := `{"host_id":"` + registerEnvelope.Data.HostID + `","name":"caster-a","listen_port":4202,"worker_count":4,"max_worker_count":16,"config_version":17,"restart_policy":"on_failure","start_immediately":true}`
	runtimeRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes", runtimeBody)
	if runtimeRec.Code != http.StatusCreated {
		t.Fatalf("create runtime status = %d body = %s", runtimeRec.Code, runtimeRec.Body.String())
	}

	heartbeatBody := `{"agent_id":"` + registerEnvelope.Data.AgentID + `","host_id":"` + registerEnvelope.Data.HostID + `","sequence":1,"resources":{"cpu_usage_pct":35.2}}`
	heartbeatRec := doJSON(handler, http.MethodPost, "/api/v1/agents/heartbeat", heartbeatBody)
	if heartbeatRec.Code != http.StatusOK {
		t.Fatalf("heartbeat status = %d body = %s", heartbeatRec.Code, heartbeatRec.Body.String())
	}

	desiredRec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/agents/"+registerEnvelope.Data.AgentID+"/desired-state?since_version=0", nil)
	handler.ServeHTTP(desiredRec, req)
	if desiredRec.Code != http.StatusOK {
		t.Fatalf("desired status = %d body = %s", desiredRec.Code, desiredRec.Body.String())
	}
	var desiredEnvelope struct {
		Data struct {
			Version  int64 `json:"version"`
			Runtimes []struct {
				RuntimeID     string `json:"runtime_id"`
				DesiredState  string `json:"desired_state"`
				RestartPolicy string `json:"restart_policy"`
			} `json:"runtimes"`
		} `json:"data"`
	}
	decodeBody(t, desiredRec, &desiredEnvelope)
	if desiredEnvelope.Data.Version == 0 || len(desiredEnvelope.Data.Runtimes) != 1 {
		t.Fatalf("unexpected desired response: %#v", desiredEnvelope.Data)
	}
	runtime := desiredEnvelope.Data.Runtimes[0]
	if runtime.DesiredState != "running" || runtime.RestartPolicy != "on_failure" {
		t.Fatalf("unexpected runtime desired: %#v", runtime)
	}
}

func TestRuntimeActionIntentDoesNotExecuteProcess(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()

	runtimeRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes", `{"host_id":"host_local","name":"caster-a","listen_port":4202,"worker_count":4,"max_worker_count":16,"config_version":17,"restart_policy":"on_failure","start_immediately":true}`)
	if runtimeRec.Code != http.StatusCreated {
		t.Fatalf("create runtime status = %d body = %s", runtimeRec.Code, runtimeRec.Body.String())
	}
	var runtimeEnvelope struct {
		Data struct {
			RuntimeID string `json:"runtime_id"`
		} `json:"data"`
	}
	decodeBody(t, runtimeRec, &runtimeEnvelope)

	drainRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes/"+runtimeEnvelope.Data.RuntimeID+"/actions/drain", `{"request_id":"req-drain"}`)
	if drainRec.Code != http.StatusAccepted {
		t.Fatalf("drain status = %d body = %s", drainRec.Code, drainRec.Body.String())
	}
	var intentEnvelope struct {
		Data struct {
			IntentID       string `json:"intent_id"`
			RequestID      string `json:"request_id"`
			Status         string `json:"status"`
			RuntimeID      string `json:"runtime_id"`
			DesiredVersion int64  `json:"desired_version"`
		} `json:"data"`
	}
	decodeBody(t, drainRec, &intentEnvelope)
	if intentEnvelope.Data.IntentID == "" || intentEnvelope.Data.Status != "accepted" || intentEnvelope.Data.RequestID != "req-drain" {
		t.Fatalf("unexpected intent response: %#v", intentEnvelope.Data)
	}
	if intentEnvelope.Data.RuntimeID != runtimeEnvelope.Data.RuntimeID || intentEnvelope.Data.DesiredVersion == 0 {
		t.Fatalf("unexpected desired version response: %#v", intentEnvelope.Data)
	}

	stopRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes/"+runtimeEnvelope.Data.RuntimeID+"/actions/stop", `{"request_id":"req-stop"}`)
	if stopRec.Code != http.StatusAccepted {
		t.Fatalf("stop status = %d body = %s", stopRec.Code, stopRec.Body.String())
	}
	intentsRec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/control/runtimes/"+runtimeEnvelope.Data.RuntimeID+"/intents?limit=10", nil)
	handler.ServeHTTP(intentsRec, req)
	if intentsRec.Code != http.StatusOK {
		t.Fatalf("intents status = %d body = %s", intentsRec.Code, intentsRec.Body.String())
	}
	var intentsEnvelope struct {
		Data []struct {
			RequestID string `json:"request_id"`
			Status    string `json:"status"`
		} `json:"data"`
	}
	decodeBody(t, intentsRec, &intentsEnvelope)
	if len(intentsEnvelope.Data) != 2 || intentsEnvelope.Data[0].Status != "accepted" || intentsEnvelope.Data[1].Status != "superseded" {
		t.Fatalf("unexpected intents response: %#v", intentsEnvelope.Data)
	}
}

func TestAgentRuntimeEventsAndMetrics(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()

	registerRec := doJSON(handler, http.MethodPost, "/api/v1/agents/register", `{"hostname":"caster-node-02","machine_id":"machine-2","os":"windows","arch":"amd64"}`)
	if registerRec.Code != http.StatusCreated {
		t.Fatalf("register status = %d body = %s", registerRec.Code, registerRec.Body.String())
	}
	var registerEnvelope struct {
		Data struct {
			AgentID string `json:"agent_id"`
			HostID  string `json:"host_id"`
		} `json:"data"`
	}
	decodeBody(t, registerRec, &registerEnvelope)

	metricsBody := `{"agent_id":"` + registerEnvelope.Data.AgentID + `","host_id":"` + registerEnvelope.Data.HostID + `","actual":[{"runtime_id":"rt-smoke","host_id":"` + registerEnvelope.Data.HostID + `","agent_id":"` + registerEnvelope.Data.AgentID + `","actual_state":"running","process_id":4242,"config_version":17,"config_path":"runtime.yml","config_checksum":"sha256:test","listen_port":4202,"worker_count":2,"connections":3,"mounts":1,"sources":1,"clients":2,"send_bps":128,"recv_bps":64,"loop_delay_ms_p95":7,"redis_connected":false,"observed_desired_version":9,"started_at":"2026-06-27T00:00:00Z","updated_at":"2026-06-27T00:00:01Z","last_exit_code":0}]}`
	metricsRec := doJSON(handler, http.MethodPost, "/api/v1/agents/"+registerEnvelope.Data.AgentID+"/runtime-metrics", metricsBody)
	if metricsRec.Code != http.StatusAccepted {
		t.Fatalf("metrics status = %d body = %s", metricsRec.Code, metricsRec.Body.String())
	}

	eventsBody := `{"agent_id":"` + registerEnvelope.Data.AgentID + `","host_id":"` + registerEnvelope.Data.HostID + `","events":[{"runtime_id":"rt-smoke","type":"reconcile_start","severity":"info","desired_version":9,"process_id":4242,"message":"started","occurred_at":"2026-06-27T00:00:02Z"}]}`
	eventsRec := doJSON(handler, http.MethodPost, "/api/v1/agents/"+registerEnvelope.Data.AgentID+"/runtime-events", eventsBody)
	if eventsRec.Code != http.StatusAccepted {
		t.Fatalf("events status = %d body = %s", eventsRec.Code, eventsRec.Body.String())
	}

	runtimeRec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/control/runtimes/rt-smoke", nil)
	handler.ServeHTTP(runtimeRec, req)
	if runtimeRec.Code != http.StatusOK {
		t.Fatalf("runtime status = %d body = %s", runtimeRec.Code, runtimeRec.Body.String())
	}
	var runtimeEnvelope struct {
		Data struct {
			RuntimeID string `json:"runtime_id"`
			Actual    struct {
				ActualState string `json:"actual_state"`
				ProcessID   int    `json:"process_id"`
			} `json:"actual"`
		} `json:"data"`
	}
	decodeBody(t, runtimeRec, &runtimeEnvelope)
	if runtimeEnvelope.Data.RuntimeID != "rt-smoke" || runtimeEnvelope.Data.Actual.ActualState != "running" || runtimeEnvelope.Data.Actual.ProcessID != 4242 {
		t.Fatalf("unexpected runtime actual: %#v", runtimeEnvelope.Data)
	}

	eventsQueryRec := httptest.NewRecorder()
	eventsReq := httptest.NewRequest(http.MethodGet, "/api/v1/control/runtimes/rt-smoke/events?limit=5", nil)
	handler.ServeHTTP(eventsQueryRec, eventsReq)
	if eventsQueryRec.Code != http.StatusOK {
		t.Fatalf("events query status = %d body = %s", eventsQueryRec.Code, eventsQueryRec.Body.String())
	}
	var eventsEnvelope struct {
		Data []struct {
			RuntimeID      string `json:"runtime_id"`
			Type           string `json:"type"`
			DesiredVersion int64  `json:"desired_version"`
		} `json:"data"`
	}
	decodeBody(t, eventsQueryRec, &eventsEnvelope)
	if len(eventsEnvelope.Data) != 1 || eventsEnvelope.Data[0].RuntimeID != "rt-smoke" || eventsEnvelope.Data[0].Type != "reconcile_start" {
		t.Fatalf("unexpected events response: %#v", eventsEnvelope.Data)
	}
}

func TestAgentRuntimeMetricsRejectsMismatchedPayloadIdentity(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()
	agentID, hostID := registerTestAgent(t, handler)

	tests := []struct {
		name string
		body string
	}{
		{
			name: "host",
			body: `{"agent_id":"` + agentID + `","host_id":"` + hostID + `","actual":[{"runtime_id":"rt-injected","host_id":"host_other","agent_id":"` + agentID + `","actual_state":"running","updated_at":"2026-06-27T00:00:01Z"}]}`,
		},
		{
			name: "agent",
			body: `{"agent_id":"` + agentID + `","host_id":"` + hostID + `","actual":[{"runtime_id":"rt-injected","host_id":"` + hostID + `","agent_id":"ag_other","actual_state":"running","updated_at":"2026-06-27T00:00:01Z"}]}`,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			rec := doJSON(handler, http.MethodPost, "/api/v1/agents/"+agentID+"/runtime-metrics", tt.body)
			if rec.Code != http.StatusBadRequest {
				t.Fatalf("metrics status = %d body = %s", rec.Code, rec.Body.String())
			}
		})
	}
}

func TestAgentRuntimeEventsRejectsMismatchedPayloadIdentity(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()
	agentID, hostID := registerTestAgent(t, handler)

	tests := []struct {
		name string
		body string
	}{
		{
			name: "host",
			body: `{"agent_id":"` + agentID + `","host_id":"` + hostID + `","events":[{"runtime_id":"rt-injected","host_id":"host_other","agent_id":"` + agentID + `","type":"reconcile_start","occurred_at":"2026-06-27T00:00:02Z"}]}`,
		},
		{
			name: "agent",
			body: `{"agent_id":"` + agentID + `","host_id":"` + hostID + `","events":[{"runtime_id":"rt-injected","host_id":"` + hostID + `","agent_id":"ag_other","type":"reconcile_start","occurred_at":"2026-06-27T00:00:02Z"}]}`,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			rec := doJSON(handler, http.MethodPost, "/api/v1/agents/"+agentID+"/runtime-events", tt.body)
			if rec.Code != http.StatusBadRequest {
				t.Fatalf("events status = %d body = %s", rec.Code, rec.Body.String())
			}
		})
	}
}

func TestProjectionKeyEndpoint(t *testing.T) {
	server := newTestServer()
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/control/projection-keys", nil)
	server.Handler().ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d body = %s", rec.Code, rec.Body.String())
	}
	var envelope struct {
		Data []struct {
			Pattern string `json:"pattern"`
		} `json:"data"`
	}
	decodeBody(t, rec, &envelope)
	if len(envelope.Data) == 0 {
		t.Fatal("expected registry entries")
	}
	for _, key := range envelope.Data {
		if len(key.Pattern) < 3 || key.Pattern[:3] != "v2:" {
			t.Fatalf("non-v2 key in endpoint: %q", key.Pattern)
		}
	}
}

func newTestServer() Server {
	cfg := config.Config{ServiceVersion: "test", HeartbeatInterval: 5 * time.Second}
	repo := control.NewMemoryRepository()
	return NewServer(
		cfg,
		agent.NewService(cfg, repo),
		control.NewService(repo),
		projection.NewRegistry(redisStore.DefaultRegistry()),
	)
}

func registerTestAgent(t *testing.T, handler http.Handler) (string, string) {
	t.Helper()
	registerRec := doJSON(handler, http.MethodPost, "/api/v1/agents/register", `{"hostname":"caster-node-test","machine_id":"machine-test","os":"windows","arch":"amd64"}`)
	if registerRec.Code != http.StatusCreated {
		t.Fatalf("register status = %d body = %s", registerRec.Code, registerRec.Body.String())
	}
	var registerEnvelope struct {
		Data struct {
			AgentID string `json:"agent_id"`
			HostID  string `json:"host_id"`
		} `json:"data"`
	}
	decodeBody(t, registerRec, &registerEnvelope)
	if registerEnvelope.Data.AgentID == "" || registerEnvelope.Data.HostID == "" {
		t.Fatalf("missing register IDs: %#v", registerEnvelope.Data)
	}
	return registerEnvelope.Data.AgentID, registerEnvelope.Data.HostID
}

func doJSON(handler http.Handler, method string, path string, body string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(method, path, bytes.NewBufferString(body))
	req.Header.Set("Content-Type", "application/json")
	handler.ServeHTTP(rec, req)
	return rec
}

func decodeBody(t *testing.T, rec *httptest.ResponseRecorder, dst any) {
	t.Helper()
	if err := json.Unmarshal(rec.Body.Bytes(), dst); err != nil {
		t.Fatalf("decode response failed: %v body=%s", err, rec.Body.String())
	}
}
