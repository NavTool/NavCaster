package state

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	agentruntime "navcaster/agent/internal/runtime"
)

type AgentState struct {
	AgentID            string                       `json:"agent_id,omitempty"`
	AgentSecret        string                       `json:"agent_secret_ref,omitempty"`
	HostID             string                       `json:"host_id,omitempty"`
	BootstrapStatus    string                       `json:"bootstrap_status,omitempty"`
	LastAdminEndpoint  string                       `json:"last_admin_endpoint,omitempty"`
	LastDesiredVersion int64                        `json:"last_desired_version,omitempty"`
	Desired            agentruntime.DesiredDocument `json:"desired,omitempty"`
	Runtimes           map[string]RuntimeCache      `json:"runtimes,omitempty"`
	UpdatedAt          time.Time                    `json:"updated_at"`
}

type RuntimeCache struct {
	Desired agentruntime.DesiredState `json:"desired,omitempty"`
	Actual  agentruntime.ActualState  `json:"actual,omitempty"`
	Events  []agentruntime.Event      `json:"events,omitempty"`
}

type Store struct {
	Path        string
	RuntimeRoot string
}

func NewStore(path string) Store {
	return Store{Path: path}
}

func NewStoreWithRuntimeRoot(path string, runtimeRoot string) Store {
	return Store{Path: path, RuntimeRoot: runtimeRoot}
}

func NewAgentState() *AgentState {
	return &AgentState{Runtimes: make(map[string]RuntimeCache)}
}

func (s Store) Load() (*AgentState, error) {
	if s.Path == "" {
		return nil, errors.New("state path is required")
	}
	data, err := os.ReadFile(s.Path)
	if errors.Is(err, os.ErrNotExist) {
		return NewAgentState(), nil
	}
	if err != nil {
		return nil, fmt.Errorf("read local agent state: %w", err)
	}
	if len(data) == 0 {
		return NewAgentState(), nil
	}
	var state AgentState
	if err := json.Unmarshal(data, &state); err != nil {
		return nil, fmt.Errorf("decode local agent state: %w", err)
	}
	if state.Runtimes == nil {
		state.Runtimes = make(map[string]RuntimeCache)
	}
	return &state, nil
}

func (s Store) Save(value *AgentState) error {
	if s.Path == "" {
		return errors.New("state path is required")
	}
	if value == nil {
		return errors.New("agent state is nil")
	}
	if value.Runtimes == nil {
		value.Runtimes = make(map[string]RuntimeCache)
	}
	value.UpdatedAt = time.Now().UTC()

	dir := filepath.Dir(s.Path)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return fmt.Errorf("create state directory: %w", err)
	}
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return fmt.Errorf("encode local agent state: %w", err)
	}

	tmp, err := os.CreateTemp(dir, filepath.Base(s.Path)+".*.tmp")
	if err != nil {
		return fmt.Errorf("create temp state file: %w", err)
	}
	tmpName := tmp.Name()
	defer func() {
		_ = os.Remove(tmpName)
	}()
	if _, err := tmp.Write(data); err != nil {
		_ = tmp.Close()
		return fmt.Errorf("write temp state file: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close temp state file: %w", err)
	}
	if err := os.Chmod(tmpName, 0o600); err != nil {
		return fmt.Errorf("chmod temp state file: %w", err)
	}
	if err := os.Rename(tmpName, s.Path); err != nil {
		return fmt.Errorf("replace local agent state: %w", err)
	}
	if err := s.saveRuntimeCache(value); err != nil {
		return err
	}
	return nil
}

func (s Store) saveRuntimeCache(value *AgentState) error {
	if s.RuntimeRoot == "" {
		return nil
	}
	for runtimeID, cache := range value.Runtimes {
		if runtimeID == "" {
			continue
		}
		dir := filepath.Join(s.RuntimeRoot, "runtimes", safeName(runtimeID))
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return fmt.Errorf("create runtime state directory for %s: %w", runtimeID, err)
		}
		if cache.Desired.RuntimeID != "" {
			if err := writeJSONFile(filepath.Join(dir, "desired.json"), cache.Desired); err != nil {
				return fmt.Errorf("write desired cache for %s: %w", runtimeID, err)
			}
		}
		if cache.Actual.RuntimeID != "" {
			if err := writeJSONFile(filepath.Join(dir, "actual.json"), cache.Actual); err != nil {
				return fmt.Errorf("write actual cache for %s: %w", runtimeID, err)
			}
		}
		if len(cache.Events) > 0 {
			if err := writeEventsLog(filepath.Join(dir, "events.log"), cache.Events); err != nil {
				return fmt.Errorf("write event cache for %s: %w", runtimeID, err)
			}
		}
	}
	return nil
}

func writeJSONFile(path string, value any) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

func writeEventsLog(path string, events []agentruntime.Event) error {
	tmp := path + ".tmp"
	file, err := os.OpenFile(tmp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return err
	}
	encoder := json.NewEncoder(file)
	for _, event := range events {
		if err := encoder.Encode(event); err != nil {
			_ = file.Close()
			_ = os.Remove(tmp)
			return err
		}
	}
	if err := file.Close(); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

func safeName(value string) string {
	result := make([]rune, 0, len(value))
	for _, r := range value {
		switch {
		case r >= 'a' && r <= 'z':
			result = append(result, r)
		case r >= 'A' && r <= 'Z':
			result = append(result, r)
		case r >= '0' && r <= '9':
			result = append(result, r)
		case r == '-', r == '_', r == '.':
			result = append(result, r)
		default:
			result = append(result, '_')
		}
	}
	if len(result) == 0 {
		return "runtime"
	}
	return string(result)
}

func (s *AgentState) ApplyDesired(doc agentruntime.DesiredDocument) {
	if s.Runtimes == nil {
		s.Runtimes = make(map[string]RuntimeCache)
	}
	if doc.Version <= 0 || doc.Version < s.LastDesiredVersion {
		return
	}
	s.LastDesiredVersion = doc.Version
	for _, desired := range doc.Runtimes {
		if desired.RuntimeID == "" {
			continue
		}
		cache := s.Runtimes[desired.RuntimeID]
		cache.Desired = desired
		s.Runtimes[desired.RuntimeID] = cache
	}
	s.Desired = agentruntime.DesiredDocument{
		Version:   s.LastDesiredVersion,
		Runtimes:  s.DesiredStates(),
		UpdatedAt: doc.UpdatedAt,
	}
}

func (s *AgentState) MarkDesiredApplied(runtimeID string, desiredVersion int64) {
	if runtimeID == "" || desiredVersion <= 0 || s.Runtimes == nil {
		return
	}
	cache, ok := s.Runtimes[runtimeID]
	if !ok {
		return
	}
	if cache.Actual.RuntimeID != "" && cache.Actual.ObservedDesiredVersion < desiredVersion {
		cache.Actual.ObservedDesiredVersion = desiredVersion
	}
	if cache.Desired.RuntimeID == runtimeID && cache.Desired.DesiredState == agentruntime.DesiredStateDeleted {
		cache.Desired = agentruntime.DesiredState{}
	}
	s.Runtimes[runtimeID] = cache
}

func (s *AgentState) UpdateActual(actual agentruntime.ActualState) {
	if actual.RuntimeID == "" {
		return
	}
	if s.Runtimes == nil {
		s.Runtimes = make(map[string]RuntimeCache)
	}
	cache := s.Runtimes[actual.RuntimeID]
	cache.Actual = actual
	s.Runtimes[actual.RuntimeID] = cache
}

func (s *AgentState) AppendEvent(event agentruntime.Event, limit int) {
	if event.RuntimeID == "" {
		return
	}
	if limit <= 0 {
		limit = 100
	}
	if s.Runtimes == nil {
		s.Runtimes = make(map[string]RuntimeCache)
	}
	cache := s.Runtimes[event.RuntimeID]
	cache.Events = append(cache.Events, event)
	if len(cache.Events) > limit {
		cache.Events = cache.Events[len(cache.Events)-limit:]
	}
	s.Runtimes[event.RuntimeID] = cache
}

func (s *AgentState) ActualStates() []agentruntime.ActualState {
	if s == nil || len(s.Runtimes) == 0 {
		return nil
	}
	actual := make([]agentruntime.ActualState, 0, len(s.Runtimes))
	for _, cache := range s.Runtimes {
		if cache.Actual.RuntimeID != "" {
			actual = append(actual, cache.Actual)
		}
	}
	return actual
}

func (s *AgentState) DesiredStates() []agentruntime.DesiredState {
	if s == nil || len(s.Runtimes) == 0 {
		return nil
	}
	desired := make([]agentruntime.DesiredState, 0, len(s.Runtimes))
	for _, cache := range s.Runtimes {
		if cache.Desired.RuntimeID != "" {
			desired = append(desired, cache.Desired)
		}
	}
	return desired
}

func (s *AgentState) BufferedEvents() []agentruntime.Event {
	if s == nil || len(s.Runtimes) == 0 {
		return nil
	}
	var events []agentruntime.Event
	for _, cache := range s.Runtimes {
		events = append(events, cache.Events...)
	}
	return events
}

func (s *AgentState) ClearEvents() {
	if s == nil || len(s.Runtimes) == 0 {
		return
	}
	for runtimeID, cache := range s.Runtimes {
		cache.Events = nil
		s.Runtimes[runtimeID] = cache
	}
}
