package config

import (
	"os"
	"strconv"
	"time"
)

type Config struct {
	Address           string
	ServiceVersion    string
	BootstrapToken    string
	PostgreSQLDSN     string
	RedisAddress      string
	HeartbeatInterval time.Duration
}

func LoadFromEnv() Config {
	return Config{
		Address:           envString("NAVCASTER_ADMIN_ADDR", ":8080"),
		ServiceVersion:    envString("NAVCASTER_ADMIN_VERSION", "dev"),
		BootstrapToken:    os.Getenv("NAVCASTER_ADMIN_BOOTSTRAP_TOKEN"),
		PostgreSQLDSN:     os.Getenv("NAVCASTER_ADMIN_POSTGRES_DSN"),
		RedisAddress:      os.Getenv("NAVCASTER_ADMIN_REDIS_ADDR"),
		HeartbeatInterval: time.Duration(envInt("NAVCASTER_ADMIN_HEARTBEAT_MS", 5000)) * time.Millisecond,
	}
}

func envString(name string, fallback string) string {
	value := os.Getenv(name)
	if value == "" {
		return fallback
	}
	return value
}

func envInt(name string, fallback int) int {
	value := os.Getenv(name)
	if value == "" {
		return fallback
	}
	parsed, err := strconv.Atoi(value)
	if err != nil || parsed <= 0 {
		return fallback
	}
	return parsed
}
