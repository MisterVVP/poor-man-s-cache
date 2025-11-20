package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
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

func spawnWorkers(cfg Config) error {
	for i := range cfg.WorkerCount {
		idx := i

		go func() {
			for {
				if err := startWorkerOnce(cfg, idx); err != nil {
					fmt.Fprintf(os.Stderr, "worker %d crashed: %v\n", idx, err)
				} else {
					fmt.Fprintf(os.Stderr, "worker %d exited, restarting\n", idx)
				}
				time.Sleep(1 * time.Second)
			}
		}()

		time.Sleep(60 * time.Millisecond)
	}

	select {}
}

func startWorkerOnce(cfg Config, i int) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	node := getNUMANodeForCPU(i)
	if err := setNUMAPolicyForNode(node); err != nil {
		fmt.Fprintf(os.Stderr, "warning: NUMA bind cpu %d node %d failed: %v\n", i, node, err)
	}

	port := cfg.BasePort + i

	args := []string{
		"--listen", strconv.Itoa(port),
	}

	cmd := exec.Command(cfg.ServerPath, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("worker %d start failed: %w", i, err)
	}

	var mask unix.CPUSet
	mask.Set(i)
	if err := unix.SchedSetaffinity(cmd.Process.Pid, &mask); err != nil {
		fmt.Fprintf(os.Stderr, "warning: could not set CPU affinity for worker %d: %v\n", i, err)
	}

	if err := setRealtimePriority(cmd.Process.Pid); err != nil {
		fmt.Fprintf(os.Stderr, "warning: could not set RT priority for worker %d: %v\n", i, err)
	}

	fmt.Printf("Started worker %d on port %d (NUMA node %d) (CPU %d)\n",
		i, port, node, i)

	err := cmd.Wait()
	if err != nil {
		fmt.Fprintf(os.Stderr, "worker %d exited with error: %v\n", i, err)
	} else {
		fmt.Fprintf(os.Stderr, "worker %d exited cleanly\n", i)
	}

	return err
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

	fmt.Println("Launching workers...")
	err := spawnWorkers(cfg)
	if err != nil {
		log.Fatalf("fatal: %v", err)
	}
}
