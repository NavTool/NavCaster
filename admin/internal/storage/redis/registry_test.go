package redis

import "testing"

func TestDefaultRegistryRendersV2Keys(t *testing.T) {
	registry := DefaultRegistry()

	tests := []struct {
		name string
		args map[string]string
		want string
	}{
		{"access_account_auth", map[string]string{"username": "rover01"}, "v2:auth:access-account:rover01"},
		{"access_account_policy", map[string]string{"access_account_id": "aacc_001"}, "v2:auth:policy:aacc_001"},
		{"runtime_config", map[string]string{"runtime_id": "rt_001"}, "v2:config:runtime:rt_001"},
		{"host_desired_state", map[string]string{"host_id": "host_001"}, "v2:control:desired-state:host_001"},
		{"action_intent", map[string]string{"intent_id": "intent_001"}, "v2:control:intent:intent_001"},
		{"runtime_actual", map[string]string{"runtime_id": "rt_001"}, "v2:runtime:actual:rt_001"},
		{"worker_stat", map[string]string{"runtime_id": "rt_001"}, "v2:runtime:worker-stat:rt_001"},
		{"agent_heartbeat", map[string]string{"agent_id": "ag_001"}, "v2:agent:heartbeat:ag_001"},
		{"mount_stream", map[string]string{"mount": "MOUNT1"}, "v2:stream:mount:MOUNT1"},
	}

	for _, tt := range tests {
		got, err := registry.Render(tt.name, tt.args)
		if err != nil {
			t.Fatalf("Render(%s) returned error: %v", tt.name, err)
		}
		if got != tt.want {
			t.Fatalf("Render(%s) = %q, want %q", tt.name, got, tt.want)
		}
	}
}

func TestDefaultRegistryRejectsMissingPlaceholder(t *testing.T) {
	registry := DefaultRegistry()
	if _, err := registry.Render("runtime_config", map[string]string{}); err == nil {
		t.Fatal("expected missing placeholder error")
	}
}

func TestDefaultRegistryContainsNoLegacyKeys(t *testing.T) {
	registry := DefaultRegistry()
	for _, key := range registry.List() {
		if len(key.Pattern) < 3 || key.Pattern[:3] != "v2:" {
			t.Fatalf("key %s uses non-v2 pattern %q", key.Name, key.Pattern)
		}
	}
}
