package host

import (
	"crypto/sha256"
	"encoding/hex"
	"net"
	"os"
	goruntime "runtime"
	"sort"
	"strings"
	"time"
)

type Identity struct {
	HostID      string            `json:"host_id,omitempty"`
	Hostname    string            `json:"hostname"`
	OS          string            `json:"os"`
	Arch        string            `json:"arch"`
	Fingerprint string            `json:"fingerprint"`
	Labels      map[string]string `json:"labels,omitempty"`
	ObservedAt  time.Time         `json:"observed_at"`
}

func Discover(configuredHostID string) (Identity, error) {
	hostname, err := os.Hostname()
	if err != nil {
		return Identity{}, err
	}

	labels := map[string]string{
		"os":   goruntime.GOOS,
		"arch": goruntime.GOARCH,
	}
	identity := Identity{
		HostID:      strings.TrimSpace(configuredHostID),
		Hostname:    hostname,
		OS:          goruntime.GOOS,
		Arch:        goruntime.GOARCH,
		Labels:      labels,
		ObservedAt:  time.Now().UTC(),
		Fingerprint: fingerprint(hostname),
	}
	return identity, nil
}

func fingerprint(hostname string) string {
	parts := []string{strings.ToLower(hostname), goruntime.GOOS, goruntime.GOARCH}
	ifaces, err := net.Interfaces()
	if err == nil {
		var macs []string
		for _, iface := range ifaces {
			if len(iface.HardwareAddr) == 0 {
				continue
			}
			macs = append(macs, strings.ToLower(iface.HardwareAddr.String()))
		}
		sort.Strings(macs)
		parts = append(parts, macs...)
	}

	sum := sha256.Sum256([]byte(strings.Join(parts, "|")))
	return hex.EncodeToString(sum[:])[:24]
}
