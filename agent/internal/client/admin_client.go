package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"path"
	"strconv"
	"strings"
	"time"

	"navcaster/agent/internal/host"
	"navcaster/agent/internal/metrics"
	agentruntime "navcaster/agent/internal/runtime"
)

var ErrNotConfigured = errors.New("admin client is not configured")

type AdminClient struct {
	baseURL        *url.URL
	httpClient     *http.Client
	bootstrapToken string
	agentID        string
	agentSecret    string
}

type Options struct {
	BaseURL        string
	BootstrapToken string
	AgentID        string
	AgentSecret    string
	Timeout        time.Duration
}

func NewAdminClient(opts Options) (*AdminClient, error) {
	base := strings.TrimSpace(opts.BaseURL)
	if base == "" {
		return nil, ErrNotConfigured
	}
	parsed, err := url.Parse(base)
	if err != nil {
		return nil, fmt.Errorf("parse admin_url: %w", err)
	}
	if parsed.Scheme == "" || parsed.Host == "" {
		return nil, fmt.Errorf("admin_url must include scheme and host")
	}
	timeout := opts.Timeout
	if timeout <= 0 {
		timeout = 5 * time.Second
	}
	return &AdminClient{
		baseURL:        parsed,
		httpClient:     &http.Client{Timeout: timeout},
		bootstrapToken: opts.BootstrapToken,
		agentID:        opts.AgentID,
		agentSecret:    opts.AgentSecret,
	}, nil
}

func (c *AdminClient) SetCredentials(agentID, agentSecret string) {
	c.agentID = agentID
	c.agentSecret = agentSecret
}

type RegisterRequest struct {
	AgentID string        `json:"agent_id,omitempty"`
	Host    host.Identity `json:"host"`
}

type RegisterResponse struct {
	AgentID             string                        `json:"agent_id"`
	AgentSecret         string                        `json:"agent_secret"`
	HostID              string                        `json:"host_id"`
	HeartbeatIntervalMS int                           `json:"heartbeat_interval_ms,omitempty"`
	Desired             *agentruntime.DesiredDocument `json:"desired,omitempty"`
}

func (c *AdminClient) Register(ctx context.Context, req RegisterRequest) (RegisterResponse, error) {
	var resp RegisterResponse
	payload := adminRegisterRequest{
		BootstrapToken: c.bootstrapToken,
		AgentID:        req.AgentID,
		HostID:         req.Host.HostID,
		Hostname:       req.Host.Hostname,
		MachineID:      req.Host.Fingerprint,
		OS:             req.Host.OS,
		Arch:           req.Host.Arch,
		AgentVersion:   "dev",
		Labels:         req.Host.Labels,
	}
	if err := c.doJSON(ctx, http.MethodPost, "/api/v1/agents/register", nil, payload, &resp, true); err != nil {
		return RegisterResponse{}, err
	}
	if resp.AgentID != "" && resp.AgentSecret != "" {
		c.SetCredentials(resp.AgentID, resp.AgentSecret)
	}
	return resp, nil
}

type adminRegisterRequest struct {
	BootstrapToken string            `json:"bootstrap_token,omitempty"`
	AgentID        string            `json:"agent_id,omitempty"`
	HostID         string            `json:"host_id,omitempty"`
	Hostname       string            `json:"hostname"`
	MachineID      string            `json:"machine_id,omitempty"`
	OS             string            `json:"os,omitempty"`
	Arch           string            `json:"arch,omitempty"`
	AgentVersion   string            `json:"agent_version,omitempty"`
	Labels         map[string]string `json:"labels,omitempty"`
}

type HeartbeatRequest struct {
	AgentID       string                     `json:"agent_id"`
	HostID        string                     `json:"host_id"`
	Host          host.Identity              `json:"host"`
	Metrics       metrics.HostMetrics        `json:"metrics"`
	RuntimeActual []agentruntime.ActualState `json:"runtime_actual,omitempty"`
	SentAt        time.Time                  `json:"sent_at"`
}

type HeartbeatResponse struct {
	Accepted                bool                          `json:"accepted"`
	ServerTime              string                        `json:"server_time,omitempty"`
	NextHeartbeatIntervalMS int                           `json:"next_heartbeat_interval_ms,omitempty"`
	Desired                 *agentruntime.DesiredDocument `json:"desired,omitempty"`
}

func (c *AdminClient) Heartbeat(ctx context.Context, agentID string, req HeartbeatRequest) (HeartbeatResponse, error) {
	var resp HeartbeatResponse
	payload := adminHeartbeatRequest{
		AgentID:          req.AgentID,
		HostID:           req.HostID,
		Resources:        map[string]any{"host": req.Host, "metrics": req.Metrics},
		RuntimeSummaries: make([]adminRuntimeBeat, 0, len(req.RuntimeActual)),
	}
	for _, actual := range req.RuntimeActual {
		payload.RuntimeSummaries = append(payload.RuntimeSummaries, adminRuntimeBeat{
			RuntimeID:              actual.RuntimeID,
			ActualState:            string(actual.ActualState),
			ProcessID:              actual.ProcessID,
			ObservedDesiredVersion: actual.ObservedDesiredVersion,
		})
	}
	err := c.doJSON(ctx, http.MethodPost, "/api/v1/agents/heartbeat", nil, payload, &resp, false)
	return resp, err
}

type adminHeartbeatRequest struct {
	AgentID          string             `json:"agent_id"`
	HostID           string             `json:"host_id"`
	Sequence         int64              `json:"sequence,omitempty"`
	Resources        map[string]any     `json:"resources,omitempty"`
	RuntimeSummaries []adminRuntimeBeat `json:"runtime_summaries,omitempty"`
}

type adminRuntimeBeat struct {
	RuntimeID              string `json:"runtime_id"`
	ActualState            string `json:"actual_state"`
	ProcessID              int    `json:"process_id,omitempty"`
	ObservedDesiredVersion int64  `json:"observed_desired_version,omitempty"`
}

func (c *AdminClient) DesiredState(ctx context.Context, agentID string, sinceVersion int64) (agentruntime.DesiredDocument, error) {
	query := url.Values{}
	if sinceVersion > 0 {
		query.Set("since_version", strconv.FormatInt(sinceVersion, 10))
	}
	var resp agentruntime.DesiredDocument
	endpoint := "/api/v1/agents/" + url.PathEscape(agentID) + "/desired-state"
	if err := c.doJSON(ctx, http.MethodGet, endpoint, query, nil, &resp, false); err != nil {
		return agentruntime.DesiredDocument{}, err
	}
	return resp, nil
}

type RuntimeEventsRequest struct {
	AgentID string               `json:"agent_id"`
	HostID  string               `json:"host_id"`
	Events  []agentruntime.Event `json:"events"`
}

func (c *AdminClient) RuntimeEvents(ctx context.Context, agentID string, req RuntimeEventsRequest) error {
	endpoint := "/api/v1/agents/" + url.PathEscape(agentID) + "/runtime-events"
	return c.doJSON(ctx, http.MethodPost, endpoint, nil, req, nil, false)
}

type RuntimeMetricsRequest struct {
	AgentID string                     `json:"agent_id"`
	HostID  string                     `json:"host_id"`
	Actual  []agentruntime.ActualState `json:"actual"`
}

func (c *AdminClient) RuntimeMetrics(ctx context.Context, agentID string, req RuntimeMetricsRequest) error {
	endpoint := "/api/v1/agents/" + url.PathEscape(agentID) + "/runtime-metrics"
	return c.doJSON(ctx, http.MethodPost, endpoint, nil, req, nil, false)
}

func (c *AdminClient) doJSON(ctx context.Context, method, endpoint string, query url.Values, in any, out any, bootstrap bool) error {
	if c == nil || c.baseURL == nil {
		return ErrNotConfigured
	}
	target := *c.baseURL
	target.Path = path.Join(c.baseURL.Path, endpoint)
	target.RawQuery = query.Encode()

	var body io.Reader
	if in != nil {
		data, err := json.Marshal(in)
		if err != nil {
			return fmt.Errorf("encode request: %w", err)
		}
		body = bytes.NewReader(data)
	}

	req, err := http.NewRequestWithContext(ctx, method, target.String(), body)
	if err != nil {
		return err
	}
	req.Header.Set("Accept", "application/json")
	if in != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if bootstrap && c.bootstrapToken != "" {
		req.Header.Set("Authorization", "Bearer "+c.bootstrapToken)
	}
	if !bootstrap {
		if c.agentID != "" {
			req.Header.Set("X-NavCaster-Agent-ID", c.agentID)
		}
		if c.agentSecret != "" {
			req.Header.Set("X-NavCaster-Agent-Secret", c.agentSecret)
		}
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(io.LimitReader(resp.Body, 4*1024*1024))
	if err != nil {
		return err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("admin API %s %s failed: status=%d body=%s", method, endpoint, resp.StatusCode, strings.TrimSpace(string(data)))
	}
	if out == nil {
		return nil
	}
	var envelope struct {
		Data  json.RawMessage `json:"data"`
		Error *struct {
			Code    string `json:"code"`
			Message string `json:"message"`
		} `json:"error"`
	}
	if err := json.Unmarshal(data, &envelope); err == nil {
		if envelope.Error != nil {
			return fmt.Errorf("admin API %s %s failed: %s: %s", method, endpoint, envelope.Error.Code, envelope.Error.Message)
		}
		if len(envelope.Data) > 0 {
			if err := json.Unmarshal(envelope.Data, out); err != nil {
				return fmt.Errorf("decode response data: %w", err)
			}
			return nil
		}
	}
	if err := json.Unmarshal(data, out); err != nil {
		return fmt.Errorf("decode response: %w", err)
	}
	return nil
}
