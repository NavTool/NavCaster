package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"time"

	"navcaster/agent/internal/config"
	"navcaster/agent/internal/service"
)

func main() {
	if len(os.Args) > 1 && os.Args[1] == "dummy-runtime" {
		runDummyRuntime()
		return
	}

	var configPath string
	var once bool
	flag.StringVar(&configPath, "config", "", "path to navcaster-agent JSON config")
	flag.BoolVar(&once, "once", false, "run one register/poll/reconcile iteration and exit")
	flag.Parse()

	cfg, err := config.Load(configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "load config: %v\n", err)
		os.Exit(2)
	}

	logger := log.New(os.Stdout, "navcaster-agent ", log.LstdFlags|log.LUTC)
	agent, err := service.NewAgent(cfg, logger)
	if err != nil {
		fmt.Fprintf(os.Stderr, "create agent: %v\n", err)
		os.Exit(2)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	if err := agent.Run(ctx, once); err != nil && err != context.Canceled {
		fmt.Fprintf(os.Stderr, "agent stopped: %v\n", err)
		os.Exit(1)
	}
}

func runDummyRuntime() {
	ch := make(chan os.Signal, 1)
	signal.Notify(ch, os.Interrupt)
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ch:
			return
		case <-ticker.C:
		}
	}
}
