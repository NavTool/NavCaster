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
	"navcaster-admin/internal/identity"
	"navcaster-admin/internal/projection"
	postgresStore "navcaster-admin/internal/storage/postgres"
	redisStore "navcaster-admin/internal/storage/redis"
)

func main() {
	cfg := config.LoadFromEnv()
	registry := projection.NewRegistry(redisStore.DefaultRegistry())
	controlRepo, identityRepo, authProjector := buildRepositories(cfg, registry)
	controlSvc := control.NewService(controlRepo)
	agentSvc := agent.NewService(cfg, controlRepo)
	identitySvc := identity.NewService(identityRepo, authProjector)
	server := api.NewServer(cfg, agentSvc, controlSvc, identitySvc, registry)

	log.Printf("navcaster-admin listening on %s", cfg.Address)
	if err := http.ListenAndServe(cfg.Address, server.Handler()); err != nil {
		log.Fatal(err)
	}
}

func buildRepositories(cfg config.Config, registry projection.Registry) (control.Repository, identity.Repository, identity.AuthProjector) {
	var repo control.Repository
	var identityRepo identity.Repository
	if cfg.PostgreSQLDSN == "" {
		log.Printf("NAVCASTER_ADMIN_POSTGRES_DSN is not set; using in-memory control repository")
		repo = control.NewMemoryRepository()
		identityRepo = identity.NewMemoryRepository()
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
		identityRepo = postgresStore.NewIdentityRepository(db)
	}
	var authProjector identity.AuthProjector = identity.NoopProjector{}
	if cfg.RedisAddress == "" {
		log.Printf("NAVCASTER_ADMIN_REDIS_ADDR is not set; Redis projection disabled")
		return repo, identityRepo, authProjector
	}
	publisher := projection.NewRedisPublisher(registry, redisStore.NewClient(redisStore.Config{Address: cfg.RedisAddress}))
	authProjector = publisher
	return control.NewProjectingRepository(repo, publisher), identityRepo, authProjector
}
