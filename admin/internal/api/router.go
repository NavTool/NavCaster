package api

import (
	"context"
	"net/http"
	"time"

	"navcaster-admin/internal/agent"
	"navcaster-admin/internal/config"
	"navcaster-admin/internal/control"
	"navcaster-admin/internal/projection"
	postgresStore "navcaster-admin/internal/storage/postgres"
	redisStore "navcaster-admin/internal/storage/redis"
)

type Server struct {
	cfg        config.Config
	agent      agent.Service
	control    control.Service
	projection projection.Registry
}

func NewServer(cfg config.Config, agentSvc agent.Service, controlSvc control.Service, registry projection.Registry) Server {
	return Server{
		cfg:        cfg,
		agent:      agentSvc,
		control:    controlSvc,
		projection: registry,
	}
}

func (s Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/v1/health", s.health)
	mux.HandleFunc("POST /api/v1/agents/register", s.registerAgent)
	mux.HandleFunc("POST /api/v1/agents/heartbeat", s.heartbeatAgent)
	mux.HandleFunc("GET /api/v1/agents/{agent_id}/desired-state", s.agentDesiredState)
	mux.HandleFunc("POST /api/v1/agents/{agent_id}/runtime-events", s.agentRuntimeEvents)
	mux.HandleFunc("POST /api/v1/agents/{agent_id}/runtime-metrics", s.agentRuntimeMetrics)
	mux.HandleFunc("GET /api/v1/control/hosts", s.listHosts)
	mux.HandleFunc("GET /api/v1/control/hosts/{host_id}", s.getHost)
	mux.HandleFunc("GET /api/v1/control/runtimes", s.listRuntimes)
	mux.HandleFunc("POST /api/v1/control/runtimes", s.createRuntime)
	mux.HandleFunc("GET /api/v1/control/runtimes/{runtime_id}", s.getRuntime)
	mux.HandleFunc("PUT /api/v1/control/runtimes/{runtime_id}/desired-state", s.updateRuntimeDesiredState)
	mux.HandleFunc("POST /api/v1/control/runtimes/{runtime_id}/actions/start", s.startRuntime)
	mux.HandleFunc("POST /api/v1/control/runtimes/{runtime_id}/actions/stop", s.stopRuntime)
	mux.HandleFunc("POST /api/v1/control/runtimes/{runtime_id}/actions/restart", s.restartRuntime)
	mux.HandleFunc("POST /api/v1/control/runtimes/{runtime_id}/actions/drain", s.drainRuntime)
	mux.HandleFunc("POST /api/v1/control/runtimes/{runtime_id}/actions/undrain", s.undrainRuntime)
	mux.HandleFunc("GET /api/v1/control/projection-keys", s.listProjectionKeys)
	return withRequestID(mux)
}

func (s Server) health(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Second)
	defer cancel()
	writeData(w, r, http.StatusOK, map[string]any{
		"service":  "navcaster-admin",
		"status":   "ok",
		"version":  s.cfg.ServiceVersion,
		"postgres": postgresStore.CheckHealth(ctx, postgresStore.Config{DSN: s.cfg.PostgreSQLDSN}),
		"redis":    redisStore.CheckHealth(ctx, redisStore.Config{Address: s.cfg.RedisAddress}),
		"time":     time.Now().UTC().Format(time.RFC3339Nano),
	})
}

func (s Server) registerAgent(w http.ResponseWriter, r *http.Request) {
	var req agent.RegisterRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.agent.Register(req)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusCreated, result)
}

func (s Server) heartbeatAgent(w http.ResponseWriter, r *http.Request) {
	var req agent.HeartbeatRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.agent.Heartbeat(req)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, result)
}

func (s Server) agentDesiredState(w http.ResponseWriter, r *http.Request) {
	since, err := parseInt64Query(r, "since_version", 0)
	if err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.agent.DesiredState(r.PathValue("agent_id"), since)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, result)
}

func (s Server) agentRuntimeEvents(w http.ResponseWriter, r *http.Request) {
	var req agent.RuntimeEventsRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.agent.RuntimeEvents(r.PathValue("agent_id"), req)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusAccepted, result)
}

func (s Server) agentRuntimeMetrics(w http.ResponseWriter, r *http.Request) {
	var req agent.RuntimeMetricsRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	result, err := s.agent.RuntimeMetrics(r.PathValue("agent_id"), req)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusAccepted, result)
}

func (s Server) listHosts(w http.ResponseWriter, r *http.Request) {
	hosts, err := s.control.ListHosts()
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, hosts)
}

func (s Server) getHost(w http.ResponseWriter, r *http.Request) {
	host, err := s.control.GetHost(r.PathValue("host_id"))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, host)
}

func (s Server) listRuntimes(w http.ResponseWriter, r *http.Request) {
	runtimes, err := s.control.ListRuntimes()
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, runtimes)
}

func (s Server) createRuntime(w http.ResponseWriter, r *http.Request) {
	var req control.RuntimeCreateRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	runtime, err := s.control.CreateRuntime(req)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusCreated, runtime)
}

func (s Server) getRuntime(w http.ResponseWriter, r *http.Request) {
	runtime, err := s.control.GetRuntime(r.PathValue("runtime_id"))
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, runtime)
}

func (s Server) updateRuntimeDesiredState(w http.ResponseWriter, r *http.Request) {
	var req control.DesiredUpdateRequest
	if err := decodeJSON(r, &req); err != nil {
		writeError(w, r, err)
		return
	}
	runtime, err := s.control.UpdateDesiredState(r.PathValue("runtime_id"), req)
	if err != nil {
		writeError(w, r, err)
		return
	}
	writeData(w, r, http.StatusOK, runtime)
}

func (s Server) startRuntime(w http.ResponseWriter, r *http.Request) {
	intent, runtime, err := s.control.Start(r.PathValue("runtime_id"))
	s.writeIntent(w, r, intent, runtime, err)
}

func (s Server) stopRuntime(w http.ResponseWriter, r *http.Request) {
	intent, runtime, err := s.control.Stop(r.PathValue("runtime_id"))
	s.writeIntent(w, r, intent, runtime, err)
}

func (s Server) drainRuntime(w http.ResponseWriter, r *http.Request) {
	intent, runtime, err := s.control.Drain(r.PathValue("runtime_id"))
	s.writeIntent(w, r, intent, runtime, err)
}

func (s Server) undrainRuntime(w http.ResponseWriter, r *http.Request) {
	intent, runtime, err := s.control.Undrain(r.PathValue("runtime_id"))
	s.writeIntent(w, r, intent, runtime, err)
}

func (s Server) restartRuntime(w http.ResponseWriter, r *http.Request) {
	intent, runtime, err := s.control.Restart(r.PathValue("runtime_id"))
	s.writeIntent(w, r, intent, runtime, err)
}

func (s Server) writeIntent(w http.ResponseWriter, r *http.Request, intent control.ActionIntent, runtime control.Runtime, err error) {
	if err != nil {
		writeError(w, r, err)
		return
	}
	desiredVersion := intent.DesiredVersion
	if desiredVersion == 0 && runtime.Desired != nil {
		desiredVersion = runtime.Desired.Version
	}
	writeData(w, r, http.StatusAccepted, map[string]any{
		"intent_id":       intent.ID,
		"status":          intent.Status,
		"runtime_id":      intent.RuntimeID,
		"desired_version": desiredVersion,
	})
}

func (s Server) listProjectionKeys(w http.ResponseWriter, r *http.Request) {
	writeData(w, r, http.StatusOK, s.projection.RedisKeys())
}
