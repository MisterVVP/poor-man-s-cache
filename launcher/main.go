package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"golang.org/x/sys/unix"
)

type Config struct {
	WorkerCount       int
	BasePort          int
	ServerPath        string
	MetricsPortBase   int
	MetricsHost       string
	DiscoveryAddr     string
	DiscoveryEndpoint string
	UseHugeTLB        bool
}

func getEnvInt(key string, defaultVal int) int {
	raw := os.Getenv(key)
	if raw == "" {
		return defaultVal
	}

	v, err := strconv.Atoi(raw)
	if err != nil {
		log.Printf("invalid %s=%q; using %d", key, raw, defaultVal)
		return defaultVal
	}
	return v
}

type targetGroup struct {
	Targets []string          `json:"targets"`
	Labels  map[string]string `json:"labels,omitempty"`
}

func buildTargetGroups(cfg Config) []targetGroup {
	groups := make([]targetGroup, 0, cfg.WorkerCount)
	for i := 0; i < cfg.WorkerCount; i++ {
		port := cfg.MetricsPortBase + i
		groups = append(groups, targetGroup{
			Targets: []string{fmt.Sprintf("%s:%d", cfg.MetricsHost, port)},
			Labels: map[string]string{
				"shard": strconv.Itoa(i),
				"node":  "local",
			},
		})
	}
	return groups
}

func discoveryHandler(cfg Config) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}

		groups := buildTargetGroups(cfg)
		data, err := json.Marshal(groups)
		if err != nil {
			log.Printf("failed to marshal discovery targets: %v", err)
			w.WriteHeader(http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write(data)
	})
}

func startDiscoveryServer(ctx context.Context, cfg Config) {
	mux := http.NewServeMux()
	mux.Handle(cfg.DiscoveryEndpoint, discoveryHandler(cfg))

	srv := &http.Server{Addr: cfg.DiscoveryAddr, Handler: mux}

	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = srv.Shutdown(shutdownCtx)
	}()

	go func() {
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("discovery server error: %v", err)
		}
	}()
}

func spawnWorkers(ctx context.Context, cfg Config) error {
	workerProcs := make([]*os.Process, cfg.WorkerCount)

	for i := range cfg.WorkerCount {
		idx := i

		go func() {
			for {
				select {
				case <-ctx.Done():
					return
				default:
				}

				proc, err := startWorkerOnce(ctx, cfg, idx)
				if proc != nil {
					workerProcs[idx] = proc
				}

				if err != nil {
					fmt.Fprintf(os.Stderr, "worker %d crashed: %v\n", idx, err)
				} else {
					fmt.Fprintf(os.Stderr, "worker %d exited\n", idx)
				}

				select {
				case <-ctx.Done():
					return
				case <-time.After(1 * time.Second):
				}
			}
		}()

		time.Sleep(50 * time.Millisecond)
	}

	<-ctx.Done()

	fmt.Fprintf(os.Stderr, "sending SIGTERM to all workers...\n")
	for _, p := range workerProcs {
		if p != nil {
			_ = p.Signal(unix.SIGTERM)
		}
	}

	time.Sleep(1000 * time.Millisecond)

	return nil
}

func startWorkerOnce(ctx context.Context, cfg Config, i int) (*os.Process, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	node := getNUMANodeForCPU(i)
	if err := setNUMAPolicyForNode(node); err != nil {
		fmt.Fprintf(os.Stderr, "warning: NUMA bind cpu %d node %d failed: %v\n", i, node, err)
	}

        port := cfg.BasePort + i
        cmd := exec.Command(cfg.ServerPath, "--listen", strconv.Itoa(port))
        cmd.Stdout = os.Stdout
        cmd.Stderr = os.Stderr

        metricsPort := cfg.MetricsPortBase + i

        cmd.Env = append(os.Environ(),
                fmt.Sprintf("PMC_SHARD=%d", i),
                fmt.Sprintf("PMC_NODE=%s", "local"),
                fmt.Sprintf("METRICS_PORT=%d", metricsPort),
                "METRICS_PORT_OFFSET=0",
        )

	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("worker %d start failed: %w", i, err)
	}

	proc := cmd.Process

	var mask unix.CPUSet
	mask.Set(i)
	_ = unix.SchedSetaffinity(proc.Pid, &mask)
	_ = setRealtimePriority(proc.Pid)

	fmt.Printf("Started worker %d on port %d (NUMA node %d) (CPU %d)\n", i, port, node, i)

	waitDone := make(chan error, 1)
	go func() {
		waitDone <- cmd.Wait()
	}()

	select {
	case <-ctx.Done():
		_ = proc.Signal(unix.SIGTERM)
		<-waitDone
		return proc, nil

	case err := <-waitDone:
		return proc, err
	}
}

func getNUMANodeForCPU(cpu int) int {
	const sysNodePath = "/sys/devices/system/node"

	entries, err := os.ReadDir(sysNodePath)
	if err != nil {
		return 0
	}

	for _, e := range entries {
		name := e.Name()
		if !strings.HasPrefix(name, "node") {
			continue
		}
		nodeID, err := strconv.Atoi(name[4:])
		if err != nil {
			continue
		}

		cpulistPath := filepath.Join(sysNodePath, name, "cpulist")
		data, err := os.ReadFile(cpulistPath)
		if err != nil {
			continue
		}
		if cpuInList(cpu, strings.TrimSpace(string(data))) {
			return nodeID
		}
	}

	return 0
}

func cpuInList(cpu int, list string) bool {
	if list == "" {
		return false
	}
	parts := strings.SplitSeq(list, ",")
	for p := range parts {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		if strings.Contains(p, "-") {
			r := strings.SplitN(p, "-", 2)
			low, err1 := strconv.Atoi(strings.TrimSpace(r[0]))
			hi, err2 := strconv.Atoi(strings.TrimSpace(r[1]))
			if err1 == nil && err2 == nil && cpu >= low && cpu <= hi {
				return true
			}
		} else {
			val, err := strconv.Atoi(p)
			if err == nil && cpu == val {
				return true
			}
		}
	}
	return false
}

func setNUMAPolicyForNode(node int) error {
	var mask unix.CPUSet
	if node < 0 {
		node = 0
	}
	if node >= 64 {
		node = 63
	}
	mask.Set(node)

	err := unix.SetMemPolicy(unix.MPOL_BIND, &mask)
	if err != nil {
		return err
	}
	return nil
}

func setRealtimePriority(pid int) error {
	prioStr := os.Getenv("PMC_RT_PRIO")
	if prioStr == "" {
		return nil
	}
	prio, err := strconv.Atoi(prioStr)
	if err != nil {
		return fmt.Errorf("invalid PMC_RT_PRIO %q: %w", prioStr, err)
	}

	attr := &unix.SchedAttr{
		Priority: uint32(prio),
	}
	if err := unix.SchedSetAttr(pid, attr, unix.SCHED_FIFO); err != nil {
		return err
	}
	return nil
}

func main() {
	if len(os.Args) < 4 {
		log.Fatal("Usage: pmc-cluster-controller <server-binary> <worker-count> <base-port>")
	}

	workers, _ := strconv.Atoi(os.Args[2])
	basePort, _ := strconv.Atoi(os.Args[3])

        metricsPortBase := getEnvInt("METRICS_PORT", 9100)
        metricsHost := os.Getenv("PMC_SCRAPE_HOST")
        if metricsHost == "" {
                metricsHost = "cache-cluster"
        }

        metricsRangeEnd := metricsPortBase + workers
        dataRangeEnd := basePort + workers
        if metricsPortBase < dataRangeEnd && basePort < metricsRangeEnd {
                metricsPortBase = dataRangeEnd
                log.Printf("metrics port base overlaps data ports; shifting to %d", metricsPortBase)
        }

	discoveryAddr := os.Getenv("PMC_DISCOVERY_ADDR")
	if discoveryAddr == "" {
		discoveryAddr = ":9400"
	} else if !strings.Contains(discoveryAddr, ":") {
		discoveryAddr = ":" + discoveryAddr
	}

	discoveryEndpoint := os.Getenv("PMC_DISCOVERY_ENDPOINT")
	if discoveryEndpoint == "" {
		discoveryEndpoint = "/discovery"
	} else if !strings.HasPrefix(discoveryEndpoint, "/") {
		discoveryEndpoint = "/" + discoveryEndpoint
	}

	cfg := Config{
		ServerPath:        os.Args[1],
		WorkerCount:       workers,
		BasePort:          basePort,
		MetricsPortBase:   metricsPortBase,
		MetricsHost:       metricsHost,
		DiscoveryAddr:     discoveryAddr,
		DiscoveryEndpoint: discoveryEndpoint,
		UseHugeTLB:        false,
	}

	ctx, cancel := context.WithCancel(context.Background())

	startDiscoveryServer(ctx, cfg)
	fmt.Printf("Discovery endpoint available at %s%s (metrics host=%s base=%d)\n", cfg.DiscoveryAddr, cfg.DiscoveryEndpoint, cfg.MetricsHost, cfg.MetricsPortBase)

	sigs := make(chan os.Signal, 2)
	signal.Notify(sigs, unix.SIGTERM, unix.SIGINT)

	go func() {
		sig := <-sigs
		fmt.Fprintf(os.Stderr, "controller received signal: %v — shutting down...\n", sig)
		cancel() // broadcast shutdown
	}()

	fmt.Println("Launching workers...")
	if err := spawnWorkers(ctx, cfg); err != nil {
		log.Fatalf("fatal: %v", err)
	}
}
