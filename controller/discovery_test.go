package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestBuildTargetGroups(t *testing.T) {
	cfg := Config{
		WorkerCount:     3,
		MetricsPortBase: 9100,
		MetricsHost:     "cache-cluster",
	}

	groups := buildTargetGroups(cfg)
	if len(groups) != 3 {
		t.Fatalf("expected 3 groups, got %d", len(groups))
	}

	for i, g := range groups {
		expectedTarget := fmt.Sprintf("cache-cluster:%d", 9100+i)
		if len(g.Targets) != 1 || g.Targets[0] != expectedTarget {
			t.Fatalf("unexpected target for shard %d: %+v", i, g.Targets)
		}
		if g.Labels["shard"] != fmt.Sprintf("%d", i) {
			t.Fatalf("unexpected shard label: %v", g.Labels)
		}
	}
}

func TestResolveMetricsPortBase(t *testing.T) {
	base := resolveMetricsPortBase(9100, 9101, 4)
	if base != 9106 {
		t.Fatalf("expected shifted metrics base to leave a gap, got %d", base)
	}

	unchanged := resolveMetricsPortBase(9200, 9101, 4)
	if unchanged != 9200 {
		t.Fatalf("expected metrics base to stay unchanged when non-overlapping, got %d", unchanged)
	}
}

func TestDiscoveryHandler(t *testing.T) {
	cfg := Config{
		WorkerCount:       2,
		MetricsPortBase:   9200,
		MetricsHost:       "cache-cluster",
		DiscoveryEndpoint: "/discovery",
	}

	rr := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/discovery", nil)
	discoveryHandler(cfg).ServeHTTP(rr, req)

	if rr.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d", rr.Code)
	}

	var groups []targetGroup
	if err := json.Unmarshal(rr.Body.Bytes(), &groups); err != nil {
		t.Fatalf("failed to decode response: %v", err)
	}

	if len(groups) != 2 || groups[0].Targets[0] != "cache-cluster:9200" || groups[1].Targets[0] != "cache-cluster:9201" {
		t.Fatalf("unexpected discovery payload: %+v", groups)
	}
}
