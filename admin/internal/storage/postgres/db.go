package postgres

type Status string

const (
	StatusConfigured    Status = "configured"
	StatusNotConfigured Status = "not_configured"
)

type Config struct {
	DSN string
}

func HealthStatus(cfg Config) Status {
	if cfg.DSN == "" {
		return StatusNotConfigured
	}
	return StatusConfigured
}
