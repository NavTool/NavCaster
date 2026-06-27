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

func (s Service) CreateRuntime(req RuntimeCreateRequest) (Runtime, error) {
	return s.repo.CreateRuntime(req)
}

func (s Service) UpdateDesiredState(runtimeID string, req DesiredUpdateRequest) (Runtime, error) {
	return s.repo.UpdateDesiredState(runtimeID, req)
}

func (s Service) Start(runtimeID string) (ActionIntent, Runtime, error) {
	return s.repo.RecordActionIntent(runtimeID, ActionKindStart, map[string]any{"desired_state": "running"})
}

func (s Service) Stop(runtimeID string) (ActionIntent, Runtime, error) {
	return s.repo.RecordActionIntent(runtimeID, ActionKindStop, map[string]any{"desired_state": "stopped"})
}

func (s Service) Drain(runtimeID string) (ActionIntent, Runtime, error) {
	return s.repo.RecordActionIntent(runtimeID, ActionKindDrain, map[string]any{"draining": true})
}

func (s Service) Undrain(runtimeID string) (ActionIntent, Runtime, error) {
	return s.repo.RecordActionIntent(runtimeID, ActionKindUndrain, map[string]any{"draining": false})
}

func (s Service) Restart(runtimeID string) (ActionIntent, Runtime, error) {
	return s.repo.RecordActionIntent(runtimeID, ActionKindRestart, map[string]any{"desired_state": "running"})
}
