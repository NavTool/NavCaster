package control

import (
	"strings"
	"time"
)

const DefaultRuntimeStaleAfter = 60 * time.Second

func EvaluateRuntimeControl(runtime Runtime, latestIntent *ActionIntent, now time.Time, staleAfter time.Duration) RuntimeControl {
	if staleAfter <= 0 {
		staleAfter = DefaultRuntimeStaleAfter
	}
	control := RuntimeControl{
		Status:            RuntimeControlPending,
		Pending:           true,
		StaleAfterSeconds: int64(staleAfter.Seconds()),
		LatestIntent:      latestIntent,
	}
	if runtime.Desired != nil {
		control.DesiredVersion = runtime.Desired.Version
		control.DesiredState = runtime.Desired.DesiredState
	}
	if runtime.Actual != nil {
		control.ObservedDesiredVersion = runtime.Actual.ObservedDesiredVersion
		control.ActualState = runtime.Actual.ActualState
		control.LastError = runtime.Actual.LastError
		if !runtime.Actual.UpdatedAt.IsZero() {
			observedAt := runtime.Actual.UpdatedAt
			control.LastObservedAt = &observedAt
			control.Stale = now.Sub(runtime.Actual.UpdatedAt) > staleAfter
		} else {
			control.Stale = true
		}
	}

	switch {
	case runtime.Desired == nil:
		control.Reason = "desired_missing"
	case runtime.Actual == nil:
		control.Reason = "actual_missing"
	case control.Stale:
		control.Status = RuntimeControlStale
		control.Reason = "actual_stale"
	case runtime.Actual.LastError != "" && runtime.Actual.ObservedDesiredVersion >= runtime.Desired.Version:
		control.Status = RuntimeControlFailed
		control.Pending = false
		control.Failed = true
		control.Reason = "actual_error"
	case runtime.Actual.ObservedDesiredVersion < runtime.Desired.Version:
		control.Reason = "desired_not_observed"
	case runtime.Desired.ConfigVersion > 0 && runtime.Actual.ConfigVersion > 0 && runtime.Actual.ConfigVersion != runtime.Desired.ConfigVersion:
		control.Reason = "config_version_mismatch"
	case stateConverged(runtime.Desired.DesiredState, runtime.Actual.ActualState):
		control.Status = RuntimeControlConverged
		control.Pending = false
		control.Reason = "converged"
	default:
		control.Status = RuntimeControlFailed
		control.Pending = false
		control.Failed = true
		control.Reason = "state_mismatch"
	}
	return control
}

func stateConverged(desired DesiredState, actual string) bool {
	actual = strings.ToLower(strings.TrimSpace(actual))
	switch desired {
	case DesiredStateRunning:
		return actual == "running"
	case DesiredStateStopped:
		return actual == "stopped" || actual == "exited" || actual == "not_running"
	case DesiredStateDraining:
		return actual == "draining"
	case DesiredStateDeleted:
		return actual == "deleted" || actual == "stopped" || actual == "exited" || actual == "not_running"
	default:
		return false
	}
}
