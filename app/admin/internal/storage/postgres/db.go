package postgres

import (
	"context"
	"database/sql"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"time"

	_ "github.com/lib/pq"
)

type Status string

const (
	StatusConfigured    Status = "configured"
	StatusNotConfigured Status = "not_configured"
	StatusConnected     Status = "connected"
	StatusUnavailable   Status = "unavailable"
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

func CheckHealth(ctx context.Context, cfg Config) Status {
	if cfg.DSN == "" {
		return StatusNotConfigured
	}
	db, err := Open(ctx, cfg)
	if err != nil {
		return StatusUnavailable
	}
	defer db.Close()
	return StatusConnected
}

func Open(ctx context.Context, cfg Config) (*sql.DB, error) {
	if cfg.DSN == "" {
		return nil, errors.New("postgres DSN is not configured")
	}
	db, err := sql.Open("postgres", cfg.DSN)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(10)
	db.SetMaxIdleConns(5)
	db.SetConnMaxLifetime(30 * time.Minute)
	if err := db.PingContext(ctx); err != nil {
		_ = db.Close()
		return nil, err
	}
	return db, nil
}

func ApplyMigrations(ctx context.Context, db *sql.DB, migrationsDir string) error {
	sqlBytes, err := os.ReadFile(migrationPath(migrationsDir))
	if err != nil {
		return err
	}
	_, err = db.ExecContext(ctx, string(sqlBytes))
	return err
}

func migrationPath(migrationsDir string) string {
	if migrationsDir != "" {
		return filepath.Join(migrationsDir, "0001_v2_adminservice_foundation.sql")
	}
	return defaultMigrationPath()
}

func defaultMigrationPath() string {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return filepath.Join("migrations", "0001_v2_adminservice_foundation.sql")
	}
	root := filepath.Clean(filepath.Join(filepath.Dir(file), "..", "..", ".."))
	return filepath.Join(root, "migrations", "0001_v2_adminservice_foundation.sql")
}
