package metrics

import (
	goruntime "runtime"
	"time"
)

var startedAt = time.Now().UTC()

type HostMetrics struct {
	CollectedAt        time.Time `json:"collected_at"`
	AgentUptimeSeconds int64     `json:"agent_uptime_seconds"`
	GoRoutines         int       `json:"go_routines"`
	GoAllocatedBytes   uint64    `json:"go_allocated_bytes"`
	GoSysBytes         uint64    `json:"go_sys_bytes"`
	GoNumGC            uint32    `json:"go_num_gc"`
}

func Snapshot() HostMetrics {
	var mem goruntime.MemStats
	goruntime.ReadMemStats(&mem)
	now := time.Now().UTC()
	return HostMetrics{
		CollectedAt:        now,
		AgentUptimeSeconds: int64(now.Sub(startedAt).Seconds()),
		GoRoutines:         goruntime.NumGoroutine(),
		GoAllocatedBytes:   mem.Alloc,
		GoSysBytes:         mem.Sys,
		GoNumGC:            mem.NumGC,
	}
}
