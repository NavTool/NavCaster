package audit

import "time"

type OperationLog struct {
	ID         string         `json:"id"`
	RequestID  string         `json:"request_id"`
	ActorType  string         `json:"actor_type"`
	ActorID    string         `json:"actor_id"`
	Action     string         `json:"action"`
	TargetType string         `json:"target_type"`
	TargetID   string         `json:"target_id"`
	Payload    map[string]any `json:"payload,omitempty"`
	CreatedAt  time.Time      `json:"created_at"`
}

type Logger interface {
	Record(OperationLog) error
}
