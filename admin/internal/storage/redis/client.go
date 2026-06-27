package redis

type Status string

const (
	StatusConfigured    Status = "configured"
	StatusNotConfigured Status = "not_configured"
)

type Config struct {
	Address string
}

func HealthStatus(cfg Config) Status {
	if cfg.Address == "" {
		return StatusNotConfigured
	}
	return StatusConfigured
}
