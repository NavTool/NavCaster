package config

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// Duration keeps JSON configuration readable while still exposing time.Duration
// to the rest of the agent.
type Duration struct {
	time.Duration
}

func (d Duration) MarshalJSON() ([]byte, error) {
	return json.Marshal(d.String())
}

func (d *Duration) UnmarshalJSON(data []byte) error {
	raw := strings.TrimSpace(string(data))
	if raw == "" || raw == "null" {
		d.Duration = 0
		return nil
	}

	if strings.HasPrefix(raw, "\"") {
		var s string
		if err := json.Unmarshal(data, &s); err != nil {
			return err
		}
		parsed, err := time.ParseDuration(s)
		if err != nil {
			return fmt.Errorf("parse duration %q: %w", s, err)
		}
		d.Duration = parsed
		return nil
	}

	seconds, err := strconv.ParseFloat(raw, 64)
	if err != nil {
		return fmt.Errorf("parse duration seconds %q: %w", raw, err)
	}
	d.Duration = time.Duration(seconds * float64(time.Second))
	return nil
}

// Config is the local Agent configuration. Secrets are placeholders for the v2
// AdminService contract; they are only sent to AdminService and are not logged.
type Config struct {
	AdminURL          string   `json:"admin_url"`
	BootstrapToken    string   `json:"bootstrap_token"`
	AgentID           string   `json:"agent_id"`
	AgentSecret       string   `json:"agent_secret"`
	HostID            string   `json:"host_id"`
	StatePath         string   `json:"state_path"`
	RuntimeRoot       string   `json:"runtime_root"`
	HeartbeatInterval Duration `json:"heartbeat_interval"`
	PollInterval      Duration `json:"poll_interval"`
	RequestTimeout    Duration `json:"request_timeout"`
	StopTimeout       Duration `json:"stop_timeout"`
}

func Default() Config {
	return Config{
		AdminURL:          "http://127.0.0.1:8080",
		StatePath:         "agent_state.json",
		RuntimeRoot:       "runtime",
		HeartbeatInterval: Duration{Duration: 10 * time.Second},
		PollInterval:      Duration{Duration: 5 * time.Second},
		RequestTimeout:    Duration{Duration: 5 * time.Second},
		StopTimeout:       Duration{Duration: 5 * time.Second},
	}
}

func Load(path string) (Config, error) {
	cfg := Default()
	if path != "" {
		data, err := os.ReadFile(path)
		if err != nil {
			return Config{}, fmt.Errorf("read agent config: %w", err)
		}
		if err := json.Unmarshal(data, &cfg); err != nil {
			return Config{}, fmt.Errorf("decode agent config: %w", err)
		}
	}

	applyEnv(&cfg)
	applyDefaults(&cfg)
	if err := cfg.Validate(); err != nil {
		return Config{}, err
	}
	return cfg, nil
}

func applyEnv(cfg *Config) {
	setString := func(env string, target *string) {
		if value := strings.TrimSpace(os.Getenv(env)); value != "" {
			*target = value
		}
	}

	setString("NAVCASTER_AGENT_ADMIN_URL", &cfg.AdminURL)
	setString("NAVCASTER_AGENT_BOOTSTRAP_TOKEN", &cfg.BootstrapToken)
	setString("NAVCASTER_AGENT_ID", &cfg.AgentID)
	setString("NAVCASTER_AGENT_SECRET", &cfg.AgentSecret)
	setString("NAVCASTER_AGENT_HOST_ID", &cfg.HostID)
	setString("NAVCASTER_AGENT_STATE_PATH", &cfg.StatePath)
	setString("NAVCASTER_AGENT_RUNTIME_ROOT", &cfg.RuntimeRoot)

	setDuration := func(env string, target *Duration) {
		if value := strings.TrimSpace(os.Getenv(env)); value != "" {
			parsed, err := time.ParseDuration(value)
			if err == nil {
				target.Duration = parsed
			}
		}
	}
	setDuration("NAVCASTER_AGENT_HEARTBEAT_INTERVAL", &cfg.HeartbeatInterval)
	setDuration("NAVCASTER_AGENT_POLL_INTERVAL", &cfg.PollInterval)
	setDuration("NAVCASTER_AGENT_REQUEST_TIMEOUT", &cfg.RequestTimeout)
	setDuration("NAVCASTER_AGENT_STOP_TIMEOUT", &cfg.StopTimeout)
}

func applyDefaults(cfg *Config) {
	def := Default()
	if cfg.AdminURL == "" {
		cfg.AdminURL = def.AdminURL
	}
	if cfg.StatePath == "" {
		cfg.StatePath = def.StatePath
	}
	if cfg.RuntimeRoot == "" {
		cfg.RuntimeRoot = def.RuntimeRoot
	}
	if cfg.HeartbeatInterval.Duration <= 0 {
		cfg.HeartbeatInterval = def.HeartbeatInterval
	}
	if cfg.PollInterval.Duration <= 0 {
		cfg.PollInterval = def.PollInterval
	}
	if cfg.RequestTimeout.Duration <= 0 {
		cfg.RequestTimeout = def.RequestTimeout
	}
	if cfg.StopTimeout.Duration <= 0 {
		cfg.StopTimeout = def.StopTimeout
	}
}

func (c Config) Validate() error {
	if strings.TrimSpace(c.StatePath) == "" {
		return errors.New("state_path is required")
	}
	if strings.TrimSpace(c.RuntimeRoot) == "" {
		return errors.New("runtime_root is required")
	}
	if c.HeartbeatInterval.Duration <= 0 {
		return errors.New("heartbeat_interval must be positive")
	}
	if c.PollInterval.Duration <= 0 {
		return errors.New("poll_interval must be positive")
	}
	if c.RequestTimeout.Duration <= 0 {
		return errors.New("request_timeout must be positive")
	}
	if c.StopTimeout.Duration <= 0 {
		return errors.New("stop_timeout must be positive")
	}
	return nil
}

func (c Config) AbsoluteStatePath() (string, error) {
	return filepath.Abs(c.StatePath)
}

func (c Config) AbsoluteRuntimeRoot() (string, error) {
	return filepath.Abs(c.RuntimeRoot)
}
