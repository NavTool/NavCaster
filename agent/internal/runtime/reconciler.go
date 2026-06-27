package runtime

import (
	"context"
	"fmt"
	"time"
)

type ProcessManager interface {
	Actual(runtimeID string) (ActualState, bool)
	Start(ctx context.Context, desired DesiredState) (ActualState, error)
	Stop(ctx context.Context, runtimeID string, timeout time.Duration) (ActualState, error)
	Restart(ctx context.Context, desired DesiredState, timeout time.Duration) (ActualState, error)
}

type ReconcileOptions struct {
	HostID       string
	StopTimeout  time.Duration
	RenderConfig func(ctx context.Context, desired DesiredState) (path string, checksum string, err error)
}

type ReconcileAction string

const (
	ReconcileNoop    ReconcileAction = "noop"
	ReconcileStart   ReconcileAction = "start"
	ReconcileStop    ReconcileAction = "stop"
	ReconcileRestart ReconcileAction = "restart"
	ReconcileSkip    ReconcileAction = "skip"
	ReconcileError   ReconcileAction = "error"
)

type ReconcileResult struct {
	RuntimeID      string          `json:"runtime_id"`
	Action         ReconcileAction `json:"action"`
	DesiredVersion int64           `json:"desired_version,omitempty"`
	Applied        bool            `json:"applied,omitempty"`
	Actual         ActualState     `json:"actual,omitempty"`
	Error          string          `json:"error,omitempty"`
}

func Reconcile(ctx context.Context, desired []DesiredState, manager ProcessManager, opts ReconcileOptions) []ReconcileResult {
	results := make([]ReconcileResult, 0, len(desired))
	for _, item := range desired {
		result := reconcileOne(ctx, item, manager, opts)
		results = append(results, result)
	}
	return results
}

func reconcileOne(ctx context.Context, desired DesiredState, manager ProcessManager, opts ReconcileOptions) ReconcileResult {
	result := ReconcileResult{RuntimeID: desired.RuntimeID, DesiredVersion: desired.Version}
	if desired.RuntimeID == "" {
		result.Action = ReconcileError
		result.Error = "runtime_id is required"
		return result
	}
	if opts.HostID != "" && desired.HostID != "" && desired.HostID != opts.HostID {
		result.Action = ReconcileSkip
		result.Error = "desired state belongs to a different host"
		return result
	}

	actual, hasActual := manager.Actual(desired.RuntimeID)
	switch desired.DesiredState {
	case DesiredStateRunning, DesiredStateDraining:
		return reconcileRunning(ctx, desired, actual, hasActual, manager, opts)
	case DesiredStateStopped, DesiredStateDeleted:
		if !hasActual || actual.ActualState == ActualStateMissing || actual.ActualState == ActualStateStopped {
			previousObserved := actual.ObservedDesiredVersion
			result.Action = ReconcileNoop
			if actual.RuntimeID == "" {
				actual.RuntimeID = desired.RuntimeID
			}
			if actual.HostID == "" {
				actual.HostID = desired.HostID
			}
			if actual.ActualState == "" || actual.ActualState == ActualStateMissing {
				actual.ActualState = ActualStateStopped
			}
			actual.ObservedDesiredVersion = desired.Version
			actual.UpdatedAt = time.Now().UTC()
			result.Actual = actual
			result.Applied = desired.Version > 0 && previousObserved < desired.Version
			return result
		}
		stopped, err := manager.Stop(ctx, desired.RuntimeID, opts.StopTimeout)
		if stopped.RuntimeID != "" && err == nil {
			stopped.ObservedDesiredVersion = desired.Version
		}
		result.Action = ReconcileStop
		result.Actual = stopped
		if err != nil {
			result.Action = ReconcileError
			result.Error = err.Error()
		} else {
			result.Applied = true
		}
		return result
	default:
		result.Action = ReconcileError
		result.Error = fmt.Sprintf("unsupported desired_state %q", desired.DesiredState)
		return result
	}
}

func reconcileRunning(ctx context.Context, desired DesiredState, actual ActualState, hasActual bool, manager ProcessManager, opts ReconcileOptions) ReconcileResult {
	if hasActual && actual.ActualState == ActualStateFailed &&
		desired.NormalizedRestartPolicy() == RestartPolicyNever &&
		!desiredNewerThanObserved(actual, desired) {
		return ReconcileResult{RuntimeID: desired.RuntimeID, DesiredVersion: desired.Version, Action: ReconcileNoop, Actual: actual}
	}
	if hasActual && actual.ActualState == ActualStateStopped &&
		desired.NormalizedRestartPolicy() != RestartPolicyAlways &&
		!desiredNewerThanObserved(actual, desired) {
		return ReconcileResult{RuntimeID: desired.RuntimeID, DesiredVersion: desired.Version, Action: ReconcileNoop, Actual: actual}
	}

	if hasActual && (actual.IsRunning() || actual.HasLiveProcess()) {
		if shouldRestartForDesired(actual, desired) {
			return restartRuntime(ctx, desired, manager, opts)
		}
		if actual.ActualState == ActualStateFailed {
			return restartRuntime(ctx, desired, manager, opts)
		}
		return ReconcileResult{RuntimeID: desired.RuntimeID, DesiredVersion: desired.Version, Action: ReconcileNoop, Actual: actual}
	}

	rendered, err := renderDesiredConfig(ctx, desired, opts)
	if err != nil {
		return ReconcileResult{RuntimeID: desired.RuntimeID, Action: ReconcileError, Error: err.Error()}
	}
	started, err := manager.Start(ctx, rendered)
	result := ReconcileResult{RuntimeID: desired.RuntimeID, DesiredVersion: desired.Version, Action: ReconcileStart, Actual: started}
	if err != nil {
		result.Action = ReconcileError
		result.Error = err.Error()
	} else {
		result.Applied = true
	}
	return result
}

func restartRuntime(ctx context.Context, desired DesiredState, manager ProcessManager, opts ReconcileOptions) ReconcileResult {
	rendered, err := renderDesiredConfig(ctx, desired, opts)
	if err != nil {
		return ReconcileResult{RuntimeID: desired.RuntimeID, Action: ReconcileError, Error: err.Error()}
	}
	actual, err := manager.Restart(ctx, rendered, opts.StopTimeout)
	result := ReconcileResult{RuntimeID: desired.RuntimeID, DesiredVersion: desired.Version, Action: ReconcileRestart, Actual: actual}
	if err != nil {
		result.Action = ReconcileError
		result.Error = err.Error()
	} else {
		result.Applied = true
	}
	return result
}

func renderDesiredConfig(ctx context.Context, desired DesiredState, opts ReconcileOptions) (DesiredState, error) {
	if opts.RenderConfig == nil {
		return desired, nil
	}
	path, checksum, err := opts.RenderConfig(ctx, desired)
	if err != nil {
		return DesiredState{}, err
	}
	desired.ConfigPath = path
	desired.ConfigChecksum = checksum
	return desired, nil
}

func shouldRestartForDesired(actual ActualState, desired DesiredState) bool {
	if actual.ConfigVersion != desired.ConfigVersion {
		return true
	}
	return desiredNewerThanObserved(actual, desired)
}

func desiredNewerThanObserved(actual ActualState, desired DesiredState) bool {
	if actual.ObservedDesiredVersion <= 0 || desired.Version <= 0 {
		return false
	}
	generation := desired.Generation
	if generation <= 0 {
		generation = desired.Version
	}
	return generation > actual.ObservedDesiredVersion
}
