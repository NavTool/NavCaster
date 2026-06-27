package main

import (
	"context"
	"log"
	"net/http"
	"time"

	"navcaster-admin/internal/agent"
	"navcaster-admin/internal/api"
	"navcaster-admin/internal/config"
	"navcaster-admin/internal/control"
	"navcaster-admin/internal/projection"
	postgresStore "navcaster-admin/internal/storage/postgres"
	redisStore "navcaster-admin/internal/storage/redis"
)

func main() {
	cfg := config.LoadFromEnv()
	repo := buildRepository(cfg)
	controlSvc := control.NewService(repo)
	agentSvc := agent.NewService(cfg, repo)
	registry := projection.NewRegistry(redisStore.DefaultRegistry())
	server := api.NewServer(cfg, agentSvc, controlSvc, registry)

	log.Printf("navcaster-admin listening on %s", cfg.Address)
	if err := http.ListenAndServe(cfg.Address, server.Handler()); err != nil {
		log.Fatal(err)
	}
}

func buildRepository(cfg config.Config) control.Repository {
	var repo control.Repository
	if cfg.PostgreSQLDSN == "" {
		log.Printf("NAVCASTER_ADMIN_POSTGRES_DSN is not set; using in-memory control repository")
		repo = control.NewMemoryRepository()
	} else {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		db, err := postgresStore.Open(ctx, postgresStore.Config{DSN: cfg.PostgreSQLDSN})
		if err != nil {
			log.Fatalf("connect postgres: %v", err)
		}
		if err := postgresStore.ApplyMigrations(ctx, db); err != nil {
			log.Fatalf("apply postgres migrations: %v", err)
		}
		repo = postgresStore.NewControlRepository(db)
	}
	if cfg.RedisAddress == "" {
		log.Printf("NAVCASTER_ADMIN_REDIS_ADDR is not set; Redis projection disabled")
		return repo
	}
	registry := projection.NewRegistry(redisStore.DefaultRegistry())
	publisher := projection.NewRedisPublisher(registry, redisStore.NewClient(redisStore.Config{Address: cfg.RedisAddress}))
	return control.NewProjectingRepository(repo, publisher)
}
