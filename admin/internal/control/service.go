package control

type Service struct {
	repo Repository
}

func NewService(repo Repository) Service {
	return Service{repo: repo}
}

func (s Service) ListHosts() ([]Host, error) {
	return s.repo.ListHosts()
}

func (s Service) GetHost(hostID string) (Host, error) {
	return s.repo.GetHost(hostID)
}

func (s Service) ListRuntimes() ([]Runtime, error) {
	return s.repo.ListRuntimes()
}

func (s Service) GetRuntime(runtimeID string) (Runtime, error) {
	return s.repo.GetRuntime(runtimeID)
}

func (s Service) ControlPlaneStatus() (ControlPlaneStatus, error) {
	return s.repo.ControlPlaneStatus()
}

func (s Service) CreateRuntime(req RuntimeCreateRequest) (Runtime, error) {
	return s.repo.CreateRuntime(req)
}

func (s Service) UpdateDesiredState(runtimeID string, req DesiredUpdateRequest) (Runtime, error) {
	return s.repo.UpdateDesiredState(runtimeID, req)
}

func (s Service) Start(runtimeID string, req ActionRequest) (ActionIntent, Runtime, error) {
	req.Payload = mergeActionPayload(req.Payload, map[string]any{"desired_state": "running"})
	return s.repo.RecordActionIntent(runtimeID, ActionKindStart, req)
}

func (s Service) Stop(runtimeID string, req ActionRequest) (ActionIntent, Runtime, error) {
	req.Payload = mergeActionPayload(req.Payload, map[string]any{"desired_state": "stopped"})
	return s.repo.RecordActionIntent(runtimeID, ActionKindStop, req)
}

func (s Service) Drain(runtimeID string, req ActionRequest) (ActionIntent, Runtime, error) {
	req.Payload = mergeActionPayload(req.Payload, map[string]any{"draining": true})
	return s.repo.RecordActionIntent(runtimeID, ActionKindDrain, req)
}

func (s Service) Undrain(runtimeID string, req ActionRequest) (ActionIntent, Runtime, error) {
	req.Payload = mergeActionPayload(req.Payload, map[string]any{"draining": false})
	return s.repo.RecordActionIntent(runtimeID, ActionKindUndrain, req)
}

func (s Service) Restart(runtimeID string, req ActionRequest) (ActionIntent, Runtime, error) {
	req.Payload = mergeActionPayload(req.Payload, map[string]any{"desired_state": "running"})
	return s.repo.RecordActionIntent(runtimeID, ActionKindRestart, req)
}

func (s Service) ListActionIntents(runtimeID string, limit int) ([]ActionIntent, error) {
	return s.repo.ListActionIntents(runtimeID, limit)
}

func (s Service) ListRuntimeEvents(runtimeID string, limit int) ([]RuntimeEvent, error) {
	return s.repo.ListRuntimeEvents(runtimeID, limit)
}

func mergeActionPayload(current map[string]any, defaults map[string]any) map[string]any {
	if current == nil {
		current = map[string]any{}
	}
	for key, value := range defaults {
		if _, ok := current[key]; !ok {
			current[key] = value
		}
	}
	return current
}
