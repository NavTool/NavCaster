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
	AdminURL          string            `json:"admin_url"`
	BootstrapToken    string            `json:"bootstrap_token"`
	AgentID           string            `json:"agent_id"`
	AgentSecret       string            `json:"agent_secret"`
	HostID            string            `json:"host_id"`
	StatePath         string            `json:"state_path"`
	RuntimeRoot       string            `json:"runtime_root"`
	CasterExecutable  string            `json:"caster_executable"`
	CasterWorkingDir  string            `json:"caster_working_dir"`
	CasterListenHost  string            `json:"caster_listen_host"`
	CasterHealthHost  string            `json:"caster_health_host"`
	CasterHealthPort  int               `json:"caster_health_port"`
	CasterRedisHost   string            `json:"caster_redis_host"`
	CasterRedisPort   int               `json:"caster_redis_port"`
	CasterEnv         map[string]string `json:"caster_env"`
	HeartbeatInterval Duration          `json:"heartbeat_interval"`
	PollInterval      Duration          `json:"poll_interval"`
	RequestTimeout    Duration          `json:"request_timeout"`
	StopTimeout       Duration          `json:"stop_timeout"`
	ProbeTimeout      Duration          `json:"probe_timeout"`
}

func Default() Config {
	return Config{
		AdminURL:          "http://127.0.0.1:8080",
		StatePath:         "agent_state.json",
		RuntimeRoot:       "runtime",
		CasterExecutable:  "navcaster-caster",
		CasterListenHost:  "127.0.0.1",
		CasterHealthHost:  "127.0.0.1",
		CasterHealthPort:  19000,
		CasterRedisHost:   "127.0.0.1",
		CasterRedisPort:   6379,
		HeartbeatInterval: Duration{Duration: 10 * time.Second},
		PollInterval:      Duration{Duration: 5 * time.Second},
		RequestTimeout:    Duration{Duration: 5 * time.Second},
		StopTimeout:       Duration{Duration: 5 * time.Second},
		ProbeTimeout:      Duration{Duration: 2 * time.Second},
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
	setString("NAVCASTER_AGENT_CASTER_EXECUTABLE", &cfg.CasterExecutable)
	setString("NAVCASTER_AGENT_CASTER_WORKING_DIR", &cfg.CasterWorkingDir)
	setString("NAVCASTER_AGENT_CASTER_LISTEN_HOST", &cfg.CasterListenHost)
	setString("NAVCASTER_AGENT_CASTER_HEALTH_HOST", &cfg.CasterHealthHost)
	setString("NAVCASTER_AGENT_CASTER_REDIS_HOST", &cfg.CasterRedisHost)

	setInt := func(env string, target *int) {
		if value := strings.TrimSpace(os.Getenv(env)); value != "" {
			parsed, err := strconv.Atoi(value)
			if err == nil {
				*target = parsed
			}
		}
	}
	setInt("NAVCASTER_AGENT_CASTER_HEALTH_PORT", &cfg.CasterHealthPort)
	setInt("NAVCASTER_AGENT_CASTER_REDIS_PORT", &cfg.CasterRedisPort)

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
	setDuration("NAVCASTER_AGENT_PROBE_TIMEOUT", &cfg.ProbeTimeout)
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
	if cfg.CasterExecutable == "" {
		cfg.CasterExecutable = def.CasterExecutable
	}
	if cfg.CasterListenHost == "" {
		cfg.CasterListenHost = def.CasterListenHost
	}
	if cfg.CasterHealthHost == "" {
		cfg.CasterHealthHost = def.CasterHealthHost
	}
	if cfg.CasterHealthPort <= 0 {
		cfg.CasterHealthPort = def.CasterHealthPort
	}
	if cfg.CasterRedisHost == "" {
		cfg.CasterRedisHost = def.CasterRedisHost
	}
	if cfg.CasterRedisPort <= 0 {
		cfg.CasterRedisPort = def.CasterRedisPort
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
	if cfg.ProbeTimeout.Duration <= 0 {
		cfg.ProbeTimeout = def.ProbeTimeout
	}
}

func (c Config) Validate() error {
	if strings.TrimSpace(c.StatePath) == "" {
		return errors.New("state_path is required")
	}
	if strings.TrimSpace(c.RuntimeRoot) == "" {
		return errors.New("runtime_root is required")
	}
	if strings.TrimSpace(c.CasterExecutable) == "" {
		return errors.New("caster_executable is required")
	}
	if c.CasterHealthPort < 0 || c.CasterHealthPort > 65535 {
		return errors.New("caster_health_port must be within 0..65535")
	}
	if c.CasterRedisPort <= 0 || c.CasterRedisPort > 65535 {
		return errors.New("caster_redis_port must be within 1..65535")
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
	if c.ProbeTimeout.Duration <= 0 {
		return errors.New("probe_timeout must be positive")
	}
	return nil
}

func (c Config) AbsoluteStatePath() (string, error) {
	return filepath.Abs(c.StatePath)
}

func (c Config) AbsoluteRuntimeRoot() (string, error) {
	return filepath.Abs(c.RuntimeRoot)
}
