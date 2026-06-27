package service

import (
	"context"
	"errors"
	"log"
	"time"

	"navcaster/agent/internal/client"
	"navcaster/agent/internal/config"
	"navcaster/agent/internal/host"
	"navcaster/agent/internal/metrics"
	agentruntime "navcaster/agent/internal/runtime"
	"navcaster/agent/internal/state"
	"navcaster/agent/internal/supervisor"
)

type Agent struct {
	Config     config.Config
	Store      state.Store
	Client     *client.AdminClient
	Supervisor *supervisor.ProcessSupervisor
	Renderer   agentruntime.ConfigRenderer
	Logger     *log.Logger
}

func NewAgent(cfg config.Config, logger *log.Logger) (*Agent, error) {
	admin, err := client.NewAdminClient(client.Options{
		BaseURL:        cfg.AdminURL,
		BootstrapToken: cfg.BootstrapToken,
		AgentID:        cfg.AgentID,
		AgentSecret:    cfg.AgentSecret,
		Timeout:        cfg.RequestTimeout.Duration,
	})
	if err != nil && !errors.Is(err, client.ErrNotConfigured) {
		return nil, err
	}
	return &Agent{
		Config:     cfg,
		Store:      state.NewStoreWithRuntimeRoot(cfg.StatePath, cfg.RuntimeRoot),
		Client:     admin,
		Supervisor: supervisor.NewProcessSupervisor(),
		Renderer:   agentruntime.NewConfigRenderer(cfg.RuntimeRoot),
		Logger:     logger,
	}, nil
}

func (a *Agent) Run(ctx context.Context, once bool) error {
	localState, err := a.Store.Load()
	if err != nil {
		return err
	}
	a.applyConfiguredIdentity(localState)
	for _, actual := range localState.ActualStates() {
		a.Supervisor.ObserveCachedActual(actual)
	}

	identity, err := host.Discover(localState.HostID)
	if err != nil {
		return err
	}
	if identity.HostID == "" {
		identity.HostID = localState.HostID
	}

	if err := a.register(ctx, localState, identity); err != nil && a.Logger != nil {
		a.Logger.Printf("agent register failed; continuing with local cache: %v", err)
	}
	if err := a.tick(ctx, localState, identity); err != nil && a.Logger != nil {
		a.Logger.Printf("agent tick failed: %v", err)
	}
	if err := a.Store.Save(localState); err != nil {
		return err
	}
	if once {
		return nil
	}

	heartbeatTicker := time.NewTicker(a.Config.HeartbeatInterval.Duration)
	defer heartbeatTicker.Stop()
	pollTicker := time.NewTicker(a.Config.PollInterval.Duration)
	defer pollTicker.Stop()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-heartbeatTicker.C:
			if err := a.heartbeat(ctx, localState, identity); err != nil && a.Logger != nil {
				a.Logger.Printf("heartbeat failed: %v", err)
			}
			if err := a.uploadEvents(ctx, localState); err != nil && a.Logger != nil {
				a.Logger.Printf("runtime event upload failed: %v", err)
			}
			if err := a.Store.Save(localState); err != nil && a.Logger != nil {
				a.Logger.Printf("save state after heartbeat failed: %v", err)
			}
		case <-pollTicker.C:
			if err := a.tick(ctx, localState, identity); err != nil && a.Logger != nil {
				a.Logger.Printf("poll/reconcile failed: %v", err)
			}
			if err := a.Store.Save(localState); err != nil && a.Logger != nil {
				a.Logger.Printf("save state after reconcile failed: %v", err)
			}
		}
	}
}

func (a *Agent) applyConfiguredIdentity(localState *state.AgentState) {
	if localState.AgentID == "" {
		localState.AgentID = a.Config.AgentID
	}
	if localState.AgentSecret == "" {
		localState.AgentSecret = a.Config.AgentSecret
	}
	if localState.HostID == "" {
		localState.HostID = a.Config.HostID
	}
	if a.Client != nil {
		a.Client.SetCredentials(localState.AgentID, localState.AgentSecret)
	}
}

func (a *Agent) register(ctx context.Context, localState *state.AgentState, identity host.Identity) error {
	if a.Client == nil {
		return client.ErrNotConfigured
	}
	resp, err := a.Client.Register(ctx, client.RegisterRequest{
		AgentID: localState.AgentID,
		Host:    identity,
	})
	if err != nil {
		return err
	}
	localState.AgentID = resp.AgentID
	localState.AgentSecret = resp.AgentSecret
	localState.HostID = resp.HostID
	localState.BootstrapStatus = "registered"
	localState.LastAdminEndpoint = a.Config.AdminURL
	if resp.Desired != nil {
		localState.ApplyDesired(*resp.Desired)
	}
	a.Client.SetCredentials(localState.AgentID, localState.AgentSecret)
	return nil
}

func (a *Agent) tick(ctx context.Context, localState *state.AgentState, identity host.Identity) error {
	if err := a.pollDesired(ctx, localState); err != nil && a.Logger != nil {
		a.Logger.Printf("desired-state poll failed; using last-known desired state: %v", err)
	}
	results := agentruntime.Reconcile(ctx, localState.Desired.Runtimes, a.Supervisor, agentruntime.ReconcileOptions{
		HostID:      localState.HostID,
		StopTimeout: a.Config.StopTimeout.Duration,
		RenderConfig: func(ctx context.Context, desired agentruntime.DesiredState) (string, string, error) {
			return a.Renderer.Render(ctx, desired)
		},
	})
	now := time.Now().UTC()
	for _, result := range results {
		if result.Actual.RuntimeID != "" {
			result.Actual.AgentID = localState.AgentID
			localState.UpdateActual(result.Actual)
		}
		if result.Action != agentruntime.ReconcileNoop && result.Action != agentruntime.ReconcileSkip {
			event := agentruntime.Event{
				RuntimeID:      result.RuntimeID,
				HostID:         localState.HostID,
				AgentID:        localState.AgentID,
				Type:           "reconcile_" + string(result.Action),
				Severity:       "info",
				DesiredVersion: localState.LastDesiredVersion,
				ProcessID:      result.Actual.ProcessID,
				OccurredAt:     now,
			}
			if result.Error != "" {
				event.Severity = "error"
				event.Message = result.Error
			}
			localState.AppendEvent(event, 100)
		}
	}
	if err := a.heartbeat(ctx, localState, identity); err != nil {
		return err
	}
	if err := a.uploadEvents(ctx, localState); err != nil && a.Logger != nil {
		a.Logger.Printf("runtime event upload failed: %v", err)
	}
	return nil
}

func (a *Agent) pollDesired(ctx context.Context, localState *state.AgentState) error {
	if a.Client == nil || localState.AgentID == "" {
		return client.ErrNotConfigured
	}
	doc, err := a.Client.DesiredState(ctx, localState.AgentID, localState.LastDesiredVersion)
	if err != nil {
		return err
	}
	if doc.Version >= localState.LastDesiredVersion {
		localState.ApplyDesired(doc)
	}
	return nil
}

func (a *Agent) heartbeat(ctx context.Context, localState *state.AgentState, identity host.Identity) error {
	if a.Client == nil || localState.AgentID == "" {
		return client.ErrNotConfigured
	}
	resp, err := a.Client.Heartbeat(ctx, localState.AgentID, client.HeartbeatRequest{
		AgentID:       localState.AgentID,
		HostID:        localState.HostID,
		Host:          identity,
		Metrics:       metrics.Snapshot(),
		RuntimeActual: localState.ActualStates(),
		SentAt:        time.Now().UTC(),
	})
	if err != nil {
		return err
	}
	if resp.Desired != nil && resp.Desired.Version >= localState.LastDesiredVersion {
		localState.ApplyDesired(*resp.Desired)
	}
	return nil
}

func (a *Agent) uploadEvents(ctx context.Context, localState *state.AgentState) error {
	if a.Client == nil || localState.AgentID == "" {
		return client.ErrNotConfigured
	}
	events := localState.BufferedEvents()
	if len(events) == 0 {
		return nil
	}
	if err := a.Client.RuntimeEvents(ctx, localState.AgentID, client.RuntimeEventsRequest{
		AgentID: localState.AgentID,
		HostID:  localState.HostID,
		Events:  events,
	}); err != nil {
		return err
	}
	localState.ClearEvents()
	return nil
}
