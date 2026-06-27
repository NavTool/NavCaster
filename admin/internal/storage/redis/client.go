package redis

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net"
	"strconv"
	"strings"
	"time"
)

type Status string

const (
	StatusConfigured    Status = "configured"
	StatusNotConfigured Status = "not_configured"
	StatusConnected     Status = "connected"
	StatusUnavailable   Status = "unavailable"
)

type Config struct {
	Address string
}

func HealthStatus(cfg Config) Status {
	if cfg.Address == "" {
		return StatusNotConfigured
	}
	return StatusConfigured
}

func CheckHealth(ctx context.Context, cfg Config) Status {
	if cfg.Address == "" {
		return StatusNotConfigured
	}
	client := NewClient(cfg)
	if err := client.Ping(ctx); err != nil {
		return StatusUnavailable
	}
	return StatusConnected
}

type Client struct {
	address string
	timeout time.Duration
}

func NewClient(cfg Config) *Client {
	return &Client{address: cfg.Address, timeout: 3 * time.Second}
}

func (c *Client) Configured() bool {
	return c != nil && c.address != ""
}

func (c *Client) Ping(ctx context.Context) error {
	_, err := c.Do(ctx, "PING")
	return err
}

func (c *Client) Set(ctx context.Context, key string, value []byte, ttl time.Duration) error {
	args := []string{"SET", key, string(value)}
	if ttl > 0 {
		args = append(args, "EX", strconv.Itoa(int(ttl.Seconds())))
	}
	_, err := c.Do(ctx, args...)
	return err
}

func (c *Client) Publish(ctx context.Context, channel string, value []byte) error {
	_, err := c.Do(ctx, "PUBLISH", channel, string(value))
	return err
}

func (c *Client) Do(ctx context.Context, args ...string) (string, error) {
	if !c.Configured() {
		return "", fmt.Errorf("redis address is not configured")
	}
	if len(args) == 0 {
		return "", fmt.Errorf("redis command is required")
	}
	dialer := net.Dialer{Timeout: c.timeout}
	conn, err := dialer.DialContext(ctx, "tcp", c.address)
	if err != nil {
		return "", err
	}
	defer conn.Close()
	deadline := time.Now().Add(c.timeout)
	if ctxDeadline, ok := ctx.Deadline(); ok && ctxDeadline.Before(deadline) {
		deadline = ctxDeadline
	}
	_ = conn.SetDeadline(deadline)
	if _, err := conn.Write(encodeRESP(args)); err != nil {
		return "", err
	}
	reader := bufio.NewReader(conn)
	return readRESP(reader)
}

func encodeRESP(args []string) []byte {
	var b strings.Builder
	b.WriteString("*")
	b.WriteString(strconv.Itoa(len(args)))
	b.WriteString("\r\n")
	for _, arg := range args {
		b.WriteString("$")
		b.WriteString(strconv.Itoa(len(arg)))
		b.WriteString("\r\n")
		b.WriteString(arg)
		b.WriteString("\r\n")
	}
	return []byte(b.String())
}

func readRESP(reader *bufio.Reader) (string, error) {
	prefix, err := reader.ReadByte()
	if err != nil {
		return "", err
	}
	line, err := reader.ReadString('\n')
	if err != nil {
		return "", err
	}
	line = strings.TrimSuffix(strings.TrimSuffix(line, "\n"), "\r")
	switch prefix {
	case '+':
		return line, nil
	case '-':
		return "", fmt.Errorf("redis error: %s", line)
	case ':':
		return line, nil
	case '$':
		n, err := strconv.Atoi(line)
		if err != nil {
			return "", err
		}
		if n < 0 {
			return "", nil
		}
		buf := make([]byte, n+2)
		if _, err := io.ReadFull(reader, buf); err != nil {
			return "", err
		}
		return string(buf[:n]), nil
	default:
		return "", fmt.Errorf("unsupported redis response prefix %q", prefix)
	}
}
