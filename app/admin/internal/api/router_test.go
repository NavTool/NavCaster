package api

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"navcaster-admin/internal/agent"
	"navcaster-admin/internal/config"
	"navcaster-admin/internal/control"
	"navcaster-admin/internal/identity"
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

func TestIdentityRegisterLoginAndAccessAccountLifecycle(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()

	registerRec := doJSON(handler, http.MethodPost, "/api/v1/auth/register", `{"username":"alice","password":"password123","display_name":"Alice"}`)
	if registerRec.Code != http.StatusCreated {
		t.Fatalf("register status = %d body = %s", registerRec.Code, registerRec.Body.String())
	}
	duplicateRec := doJSON(handler, http.MethodPost, "/api/v1/auth/register", `{"username":"alice","password":"password123","display_name":"Alice 2"}`)
	if duplicateRec.Code != http.StatusConflict {
		t.Fatalf("duplicate register status = %d body = %s", duplicateRec.Code, duplicateRec.Body.String())
	}
	assertErrorCode(t, duplicateRec, "username_reserved")

	token := loginToken(t, handler, "alice", "password123")
	sessionRec := doJSONAuth(handler, http.MethodGet, "/api/v1/auth/session", "", token)
	if sessionRec.Code != http.StatusOK {
		t.Fatalf("session status = %d body = %s", sessionRec.Code, sessionRec.Body.String())
	}

	createAccessRec := doJSONAuth(handler, http.MethodPost, "/api/v1/me/access-accounts", `{"username":"field-rover-a01","password":"password123","display_name":"Rover A01","concurrency_limit":1}`, token)
	if createAccessRec.Code != http.StatusCreated {
		t.Fatalf("create access status = %d body = %s", createAccessRec.Code, createAccessRec.Body.String())
	}
	var accessEnvelope struct {
		Data struct {
			ID     string `json:"access_account_id"`
			Status string `json:"status"`
		} `json:"data"`
	}
	decodeBody(t, createAccessRec, &accessEnvelope)
	if accessEnvelope.Data.ID == "" || accessEnvelope.Data.Status != "active" {
		t.Fatalf("unexpected access response: %#v", accessEnvelope.Data)
	}

	disableRec := doJSONAuth(handler, http.MethodPut, "/api/v1/me/access-accounts/"+accessEnvelope.Data.ID+"/status", `{"status":"disabled"}`, token)
	if disableRec.Code != http.StatusOK {
		t.Fatalf("disable access status = %d body = %s", disableRec.Code, disableRec.Body.String())
	}
	deleteRec := doJSONAuth(handler, http.MethodDelete, "/api/v1/me/access-accounts/"+accessEnvelope.Data.ID, "", token)
	if deleteRec.Code != http.StatusOK {
		t.Fatalf("delete access status = %d body = %s", deleteRec.Code, deleteRec.Body.String())
	}
	recreateRec := doJSONAuth(handler, http.MethodPost, "/api/v1/me/access-accounts", `{"username":"field-rover-a01","password":"password123","display_name":"Rover A01"}`, token)
	if recreateRec.Code != http.StatusConflict {
		t.Fatalf("recreate deleted access status = %d body = %s", recreateRec.Code, recreateRec.Body.String())
	}
	assertErrorCode(t, recreateRec, "access_username_reserved")
}

func TestIdentityAdminDeleteDisablesAccessAndLocksName(t *testing.T) {
	server, identityRepo := newTestServerWithIdentity()
	handler := server.Handler()
	adminToken := seedAndLogin(t, handler, identityRepo, "admin", identity.RoleAdmin)

	createUserRec := doJSONAuth(handler, http.MethodPost, "/api/v1/admin/accounts", `{"username":"bob","password":"password123","display_name":"Bob","role":"user","status":"active"}`, adminToken)
	if createUserRec.Code != http.StatusCreated {
		t.Fatalf("admin create user status = %d body = %s", createUserRec.Code, createUserRec.Body.String())
	}
	var userEnvelope struct {
		Data struct {
			ID string `json:"account_id"`
		} `json:"data"`
	}
	decodeBody(t, createUserRec, &userEnvelope)
	userToken := loginToken(t, handler, "bob", "password123")
	createAccessRec := doJSONAuth(handler, http.MethodPost, "/api/v1/me/access-accounts", `{"username":"bob-device","password":"password123","display_name":"Bob Device"}`, userToken)
	if createAccessRec.Code != http.StatusCreated {
		t.Fatalf("create access status = %d body = %s", createAccessRec.Code, createAccessRec.Body.String())
	}

	deleteUserRec := doJSONAuth(handler, http.MethodDelete, "/api/v1/admin/accounts/"+userEnvelope.Data.ID, "", adminToken)
	if deleteUserRec.Code != http.StatusOK {
		t.Fatalf("delete user status = %d body = %s", deleteUserRec.Code, deleteUserRec.Body.String())
	}
	var deleteEnvelope struct {
		Data struct {
			Status                     string `json:"status"`
			DisabledAccessAccountCount int    `json:"disabled_access_account_count"`
		} `json:"data"`
	}
	decodeBody(t, deleteUserRec, &deleteEnvelope)
	if deleteEnvelope.Data.Status != "deleted" || deleteEnvelope.Data.DisabledAccessAccountCount != 1 {
		t.Fatalf("unexpected delete response: %#v", deleteEnvelope.Data)
	}

	reregisterRec := doJSON(handler, http.MethodPost, "/api/v1/auth/register", `{"username":"bob","password":"password123","display_name":"Bob Again"}`)
	if reregisterRec.Code != http.StatusConflict {
		t.Fatalf("reregister deleted username status = %d body = %s", reregisterRec.Code, reregisterRec.Body.String())
	}
	assertErrorCode(t, reregisterRec, "username_reserved")
}

func TestIdentityPermissionBoundaries(t *testing.T) {
	server, identityRepo := newTestServerWithIdentity()
	handler := server.Handler()
	adminToken := seedAndLogin(t, handler, identityRepo, "admin2", identity.RoleAdmin)

	_ = doJSON(handler, http.MethodPost, "/api/v1/auth/register", `{"username":"charlie","password":"password123","display_name":"Charlie"}`)
	charlieToken := loginToken(t, handler, "charlie", "password123")
	adminRec := doJSONAuth(handler, http.MethodGet, "/api/v1/admin/accounts", "", charlieToken)
	if adminRec.Code != http.StatusForbidden {
		t.Fatalf("user admin list status = %d body = %s", adminRec.Code, adminRec.Body.String())
	}

	_ = doJSON(handler, http.MethodPost, "/api/v1/auth/register", `{"username":"dana","password":"password123","display_name":"Dana"}`)
	danaToken := loginToken(t, handler, "dana", "password123")
	danaAccessRec := doJSONAuth(handler, http.MethodPost, "/api/v1/me/access-accounts", `{"username":"dana-device","password":"password123","display_name":"Dana Device"}`, danaToken)
	if danaAccessRec.Code != http.StatusCreated {
		t.Fatalf("dana access status = %d body = %s", danaAccessRec.Code, danaAccessRec.Body.String())
	}
	var accessEnvelope struct {
		Data struct {
			ID string `json:"access_account_id"`
		} `json:"data"`
	}
	decodeBody(t, danaAccessRec, &accessEnvelope)
	foreignRec := doJSONAuth(handler, http.MethodGet, "/api/v1/me/access-accounts/"+accessEnvelope.Data.ID, "", charlieToken)
	if foreignRec.Code != http.StatusNotFound {
		t.Fatalf("foreign access status = %d body = %s", foreignRec.Code, foreignRec.Body.String())
	}

	adminListRec := doJSONAuth(handler, http.MethodGet, "/api/v1/admin/access-accounts", "", adminToken)
	if adminListRec.Code != http.StatusOK {
		t.Fatalf("admin access list status = %d body = %s", adminListRec.Code, adminListRec.Body.String())
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

	stopRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes/"+runtimeEnvelope.Data.RuntimeID+"/actions/stop", `{"request_id":"req-stop","reason":"operator requested stop"}`)
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
			Payload   struct {
				Reason string `json:"reason"`
			} `json:"payload"`
		} `json:"data"`
	}
	decodeBody(t, intentsRec, &intentsEnvelope)
	if len(intentsEnvelope.Data) != 2 || intentsEnvelope.Data[0].Status != "accepted" || intentsEnvelope.Data[1].Status != "superseded" {
		t.Fatalf("unexpected intents response: %#v", intentsEnvelope.Data)
	}
	if intentsEnvelope.Data[0].Payload.Reason != "operator requested stop" {
		t.Fatalf("action reason was not preserved in intent payload: %#v", intentsEnvelope.Data[0])
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

func TestRuntimeActualCanRecoverFailedIntent(t *testing.T) {
	server := newTestServer()
	handler := server.Handler()
	agentID, hostID := registerTestAgent(t, handler)
	runtimeRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes", `{"host_id":"`+hostID+`","name":"caster-a","listen_port":4202,"worker_count":4,"max_worker_count":16,"config_version":17,"restart_policy":"on_failure","start_immediately":false}`)
	if runtimeRec.Code != http.StatusCreated {
		t.Fatalf("create runtime status = %d body = %s", runtimeRec.Code, runtimeRec.Body.String())
	}
	var runtimeEnvelope struct {
		Data struct {
			RuntimeID string `json:"runtime_id"`
		} `json:"data"`
	}
	decodeBody(t, runtimeRec, &runtimeEnvelope)

	startRec := doJSON(handler, http.MethodPost, "/api/v1/control/runtimes/"+runtimeEnvelope.Data.RuntimeID+"/actions/start", `{"reason":"operator start"}`)
	if startRec.Code != http.StatusAccepted {
		t.Fatalf("start status = %d body = %s", startRec.Code, startRec.Body.String())
	}
	var startEnvelope struct {
		Data struct {
			DesiredVersion int64 `json:"desired_version"`
		} `json:"data"`
	}
	decodeBody(t, startRec, &startEnvelope)

	failedBody := `{"agent_id":"` + agentID + `","host_id":"` + hostID + `","actual":[{"runtime_id":"` + runtimeEnvelope.Data.RuntimeID + `","actual_state":"failed","observed_desired_version":` + strconv.FormatInt(startEnvelope.Data.DesiredVersion, 10) + `,"last_error":"health warming up","updated_at":"2026-06-27T00:00:01Z"}]}`
	failedRec := doJSON(handler, http.MethodPost, "/api/v1/agents/"+agentID+"/runtime-metrics", failedBody)
	if failedRec.Code != http.StatusAccepted {
		t.Fatalf("failed metrics status = %d body = %s", failedRec.Code, failedRec.Body.String())
	}
	runningBody := `{"agent_id":"` + agentID + `","host_id":"` + hostID + `","actual":[{"runtime_id":"` + runtimeEnvelope.Data.RuntimeID + `","actual_state":"running","process_id":4242,"config_version":17,"listen_port":4202,"worker_count":4,"redis_connected":true,"observed_desired_version":` + strconv.FormatInt(startEnvelope.Data.DesiredVersion, 10) + `,"updated_at":"2026-06-27T00:00:03Z"}]}`
	runningRec := doJSON(handler, http.MethodPost, "/api/v1/agents/"+agentID+"/runtime-metrics", runningBody)
	if runningRec.Code != http.StatusAccepted {
		t.Fatalf("running metrics status = %d body = %s", runningRec.Code, runningRec.Body.String())
	}

	intentsRec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/control/runtimes/"+runtimeEnvelope.Data.RuntimeID+"/intents?limit=10", nil)
	handler.ServeHTTP(intentsRec, req)
	if intentsRec.Code != http.StatusOK {
		t.Fatalf("intents status = %d body = %s", intentsRec.Code, intentsRec.Body.String())
	}
	var intentsEnvelope struct {
		Data []struct {
			Status        string `json:"status"`
			FailureReason string `json:"failure_reason"`
		} `json:"data"`
	}
	decodeBody(t, intentsRec, &intentsEnvelope)
	if len(intentsEnvelope.Data) == 0 || intentsEnvelope.Data[0].Status != "observed" || intentsEnvelope.Data[0].FailureReason != "" {
		t.Fatalf("intent did not recover to observed: %#v", intentsEnvelope.Data)
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
	legacyPrefix := "v2" + ":"
	for _, key := range envelope.Data {
		if strings.HasPrefix(key.Pattern, legacyPrefix) {
			t.Fatalf("v2-prefixed key in endpoint: %q", key.Pattern)
		}
	}
}

func newTestServer() Server {
	server, _ := newTestServerWithIdentity()
	return server
}

func newTestServerWithIdentity() (Server, *identity.MemoryRepository) {
	cfg := config.Config{ServiceVersion: "test", HeartbeatInterval: 5 * time.Second}
	repo := control.NewMemoryRepository()
	identityRepo := identity.NewMemoryRepository()
	server := NewServer(
		cfg,
		agent.NewService(cfg, repo),
		control.NewService(repo),
		identity.NewService(identityRepo, nil),
		projection.NewRegistry(redisStore.DefaultRegistry()),
	)
	return server, identityRepo
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

func doJSONAuth(handler http.Handler, method string, path string, body string, token string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(method, path, bytes.NewBufferString(body))
	req.Header.Set("Content-Type", "application/json")
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	handler.ServeHTTP(rec, req)
	return rec
}

func decodeBody(t *testing.T, rec *httptest.ResponseRecorder, dst any) {
	t.Helper()
	if err := json.Unmarshal(rec.Body.Bytes(), dst); err != nil {
		t.Fatalf("decode response failed: %v body=%s", err, rec.Body.String())
	}
}

func loginToken(t *testing.T, handler http.Handler, username string, password string) string {
	t.Helper()
	rec := doJSON(handler, http.MethodPost, "/api/v1/auth/login", `{"username":"`+username+`","password":"`+password+`"}`)
	if rec.Code != http.StatusOK {
		t.Fatalf("login %s status = %d body = %s", username, rec.Code, rec.Body.String())
	}
	var envelope struct {
		Data struct {
			Token string `json:"token"`
		} `json:"data"`
	}
	decodeBody(t, rec, &envelope)
	if envelope.Data.Token == "" {
		t.Fatalf("missing login token: %s", rec.Body.String())
	}
	return envelope.Data.Token
}

func seedAndLogin(t *testing.T, handler http.Handler, repo *identity.MemoryRepository, username string, role identity.Role) string {
	t.Helper()
	hash, algo, params, err := identity.HashPassword("password123")
	if err != nil {
		t.Fatalf("hash seed password: %v", err)
	}
	_, err = repo.CreateAccount(context.Background(), identity.AccountCreate{
		Username:       username,
		UsernameNorm:   identity.NormalizeUsername(username),
		DisplayName:    username,
		Role:           role,
		Status:         identity.AccountStatusActive,
		PasswordHash:   hash,
		PasswordAlgo:   algo,
		PasswordParams: params,
		CreatedVia:     identity.CreatedViaAdmin,
	})
	if err != nil {
		t.Fatalf("seed account: %v", err)
	}
	return loginToken(t, handler, username, "password123")
}

func assertErrorCode(t *testing.T, rec *httptest.ResponseRecorder, code string) {
	t.Helper()
	var envelope struct {
		Error struct {
			Code string `json:"code"`
		} `json:"error"`
	}
	decodeBody(t, rec, &envelope)
	if envelope.Error.Code != code {
		t.Fatalf("error code = %q want %q body=%s", envelope.Error.Code, code, rec.Body.String())
	}
}
