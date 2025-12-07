package main

import (
	"context"
	"fmt"
	"log"
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
	WorkerCount int
	BasePort    int
	ServerPath  string
	UseHugeTLB  bool
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
		log.Fatal("Usage: launcher <server-binary> <worker-count> <base-port>")
	}

	workers, _ := strconv.Atoi(os.Args[2])
	basePort, _ := strconv.Atoi(os.Args[3])

	cfg := Config{
		ServerPath:  os.Args[1],
		WorkerCount: workers,
		BasePort:    basePort,
		UseHugeTLB:  false,
	}

	ctx, cancel := context.WithCancel(context.Background())

	sigs := make(chan os.Signal, 2)
	signal.Notify(sigs, unix.SIGTERM, unix.SIGINT)

	go func() {
		sig := <-sigs
		fmt.Fprintf(os.Stderr, "launcher received signal: %v — shutting down...\n", sig)
		cancel() // broadcast shutdown
	}()

	fmt.Println("Launching workers...")
	if err := spawnWorkers(ctx, cfg); err != nil {
		log.Fatalf("fatal: %v", err)
	}
}
