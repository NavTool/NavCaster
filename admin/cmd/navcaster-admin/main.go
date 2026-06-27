package main

import (
	"log"
	"net/http"

	"navcaster-admin/internal/agent"
	"navcaster-admin/internal/api"
	"navcaster-admin/internal/config"
	"navcaster-admin/internal/control"
	"navcaster-admin/internal/projection"
	redisStore "navcaster-admin/internal/storage/redis"
)

func main() {
	cfg := config.LoadFromEnv()
	repo := control.NewMemoryRepository()
	controlSvc := control.NewService(repo)
	agentSvc := agent.NewService(cfg, repo)
	registry := projection.NewRegistry(redisStore.DefaultRegistry())
	server := api.NewServer(cfg, agentSvc, controlSvc, registry)

	log.Printf("navcaster-admin listening on %s", cfg.Address)
	if err := http.ListenAndServe(cfg.Address, server.Handler()); err != nil {
		log.Fatal(err)
	}
}
