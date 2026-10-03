package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"hash/crc32"
	"io"
	"math"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func testPacket(view View) []byte {
	packet := make([]byte, 64+view.Width*view.Height*4)
	copy(packet, "NGSFRM02")
	binary.LittleEndian.PutUint32(packet[8:], 2)
	binary.LittleEndian.PutUint32(packet[16:], uint32(view.Width))
	binary.LittleEndian.PutUint32(packet[20:], uint32(view.Height))
	binary.LittleEndian.PutUint64(packet[32:], uint64(len(packet)-64))
	binary.LittleEndian.PutUint64(packet[40:], uint64(len(packet)-64))
	for index := 64; index < len(packet); index += 4 {
		packet[index] = uint8(int(view.Yaw) + 17)
		packet[index+3] = 255
	}
	binary.LittleEndian.PutUint32(packet[48:], crc32.ChecksumIEEE(packet[64:]))
	binary.LittleEndian.PutUint32(packet[52:], crc32.ChecksumIEEE(packet[:64]))
	return packet
}
func TestSpatialContract(t *testing.T) {
	view, err := parseView("NGSREQ3 42 m-grid 640 360 359.9 100 1 rgba 0")
	if err != nil || view.code() != "a090-t05-d40" || !view.Flip {
		t.Fatalf("bad canonical: %+v %v", view, err)
	}
	original := view.code()
	view.Flip = !view.Flip
	if view.code() != original {
		t.Fatal("flip changed world basis")
	}
	for index := 0; index <= 80; index++ {
		value, err := parseView(fmt.Sprintf("NGSREQ3 1 m-grid 8 8 0 90 %.17g rgba 0", math.Pow(10, float64(index-40)/40)))
		if err != nil || value.code() != fmt.Sprintf("a000-t00-d%02d", index) {
			t.Fatalf("distance %d %v", index, err)
		}
	}
	for _, bad := range []string{"NGSREQ3 1 m 8 8 NaN 0 1 rgba 0", "NGSREQ3 1 m 8 8 0 0 1 rgba 2", "NGSREQ2 1 m 8 8 0 90 1 rgba", "NGSREQ3 -1 m 8 8 0 0 1 rgba 0", "NGSREQ3 1 ../x 8 8 0 0 1 rgba 0"} {
		if _, err := parseView(bad); err == nil {
			t.Fatalf("accepted %s", bad)
		}
	}
}
func TestCachePersistenceCorruptionExpiryAndBudget(t *testing.T) {
	folder := t.TempDir()
	database, err := openDatabase(filepath.Join(folder, "db.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	defer database.close()
	now := time.Now()
	cache, err := newCache(database, filepath.Join(folder, "frames"), 24*time.Hour, 500)
	if err != nil {
		t.Fatal(err)
	}
	cache.now = func() time.Time { return now }
	view := View{ID: 1, Model: "m-test", Width: 8, Height: 8, Distance: 1, Profile: "rgba"}
	packet := testPacket(view)
	if err = cache.put("first", "a000-t45-d40.ngsf", packet); err != nil {
		t.Fatal(err)
	}
	reopened, err := newCache(database, cache.root, 24*time.Hour, 500)
	if err != nil {
		t.Fatal(err)
	}
	reopened.now = cache.now
	if got, err := reopened.get("first", view, true); err != nil || !bytes.Equal(got, packet) {
		t.Fatal("persistent cache miss", err)
	}
	now = now.Add(23 * time.Hour)
	if _, err = cache.get("first", view, true); err != nil {
		t.Fatal(err)
	}
	now = now.Add(2 * time.Hour)
	if got, _ := cache.get("first", view, false); got == nil {
		t.Fatal("foreground hit was not renewed")
	}
	now = now.Add(24 * time.Hour)
	if got, _ := cache.get("first", view, false); got != nil {
		t.Fatal("stale cache survived")
	}
	cache.put("first", "first.ngsf", packet)
	os.WriteFile(filepath.Join(cache.root, "first.ngsf"), []byte("corrupt"), 0600)
	if got, _ := cache.get("first", view, true); got != nil {
		t.Fatal("corrupt cache survived")
	}
	cache.put("second", "second.ngsf", packet)
	now = now.Add(time.Second)
	cache.put("third", "third.ngsf", packet)
	if got, _ := cache.get("second", view, true); got != nil {
		t.Fatal("LRU disk budget not enforced")
	}
	if got, _ := cache.get("third", view, false); got == nil {
		t.Fatal("newest cache evicted")
	}
	os.WriteFile(filepath.Join(cache.root, "orphan.ngsf"), packet, 0600)
	if _, err = newCache(database, cache.root, 24*time.Hour, 500); err != nil {
		t.Fatal(err)
	}
	if _, err = os.Stat(filepath.Join(cache.root, "orphan.ngsf")); !os.IsNotExist(err) {
		t.Fatal("orphan survived startup")
	}
}
func TestWorkerHelper(t *testing.T) {
	if os.Getenv("GS_TEST_WORKER") != "1" {
		return
	}
	var port, model string
	for index, value := range os.Args {
		if value == "--port" {
			port = os.Args[index+1]
		}
		if value == "--instance-model" {
			model = os.Args[index+1]
		}
	}
	var count atomic.Int64
	http.HandleFunc("/health", func(writer http.ResponseWriter, request *http.Request) {
		writer.Header().Set("X-GS-Worker-Pid", strconv.Itoa(os.Getpid()))
		writer.Write([]byte("ready"))
	})
	http.HandleFunc("/frame", func(writer http.ResponseWriter, request *http.Request) {
		body, _ := io.ReadAll(request.Body)
		view, err := parseView(string(body))
		if err != nil || view.Model != model {
			writer.WriteHeader(400)
			return
		}
		time.Sleep(80 * time.Millisecond)
		renders := count.Add(1)
		writer.Header().Set("X-GS-Request-Id", strconv.FormatUint(view.ID, 10))
		writer.Header().Set("X-GS-Model-Id", view.Model)
		writer.Header().Set("X-GS-Stats", fmt.Sprintf("{\"renders\":%d}", renders))
		writer.Write(testPacket(view))
	})
	if err := http.ListenAndServe("127.0.0.1:"+port, nil); err != nil {
		os.Exit(2)
	}
	os.Exit(0)
}
func testService(t *testing.T) (*Service, *httptest.Server) {
	t.Helper()
	folder := t.TempDir()
	modelPath := filepath.Join(folder, "sample.ply")
	os.WriteFile(modelPath, []byte("fake model"), 0600)
	revision, _ := fileDigest(modelPath)
	models := map[string]Model{"m-test": {ID: "m-test", Name: "sample.ply", Revision: revision, Path: modelPath}}
	script := filepath.Join(folder, "native-helper")
	os.WriteFile(script, []byte(fmt.Sprintf("#!/bin/sh\nGS_TEST_WORKER=1 exec %q -test.run '^TestWorkerHelper$' -- \"$@\"\n", os.Args[0])), 0700)
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := listener.Addr().(*net.TCPAddr).Port
	listener.Close()
	instances, err := newInstances(models, script, folder, []int{0}, 4, port, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	database, err := openDatabase(filepath.Join(folder, "db.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	cache, err := newCache(database, filepath.Join(folder, "frames"), 24*time.Hour, 1<<20)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	service := newService(ctx, models, instances, cache, "test-renderer", strings.Repeat("a", 64), 30*time.Second)
	server := httptest.NewServer(service)
	t.Cleanup(func() { server.Close(); cancel(); service.wait(); instances.close(); database.close() })
	return service, server
}
func callService(t *testing.T, server *httptest.Server, path, body, session string, prefetch bool) (*http.Response, []byte) {
	t.Helper()
	method := "GET"
	if body != "" {
		method = "POST"
	}
	request, _ := http.NewRequest(method, server.URL+path, strings.NewReader(body))
	request.Header.Set("Authorization", "Bearer "+strings.Repeat("a", 64))
	request.Header.Set("X-GS-Session", session)
	if prefetch {
		request.Header.Set("X-GS-Prefetch", "1")
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		t.Error(err)
		return &http.Response{StatusCode: 599}, nil
	}
	defer response.Body.Close()
	packet, err := io.ReadAll(response.Body)
	if err != nil {
		t.Error(err)
	}
	return response, packet
}
func TestSameModelConcurrentUsersShareOneRenderAndCache(t *testing.T) {
	service, server := testService(t)
	var wait sync.WaitGroup
	for index := 1; index <= 20; index++ {
		wait.Add(1)
		go func(id int) {
			defer wait.Done()
			response, packet := callService(t, server, "/frame", fmt.Sprintf("NGSREQ3 %d m-test 8 8 0 0 1 rgba 0", id), "", false)
			if response.StatusCode != 200 || response.Header.Get("X-GS-Request-Id") != strconv.Itoa(id) || !validPacket(packet, View{Width: 8, Height: 8, Profile: "rgba"}) {
				t.Errorf("bad shared response %d %d", id, response.StatusCode)
			}
			if response.Header.Get("X-GS-Stats") != "{\"renders\":1}" && response.Header.Get("X-GS-Cache") != "hit" {
				t.Error("duplicate native render")
			}
		}(index)
	}
	wait.Wait()
	if service.instances.Loads.Load() != 1 || len(service.instances.status()) != 1 {
		t.Fatal("duplicate model instances")
	}
	response, _ := callService(t, server, "/frame", "NGSREQ3 99 m-test 8 8 0 0 1 rgba 0", "", false)
	if response.Header.Get("X-GS-Cache") != "hit" {
		t.Fatal("cache not reused")
	}
	service.instances.mutex.Lock()
	service.instances.loaded["m-test"].lastUser = time.Now().Add(-2 * time.Second)
	service.instances.mutex.Unlock()
	service.instances.sweep(nil)
	if len(service.instances.status()) != 0 {
		t.Fatal("idle instance not unloaded")
	}
	response, _ = callService(t, server, "/frame", "NGSREQ3 100 m-test 8 8 0 0 1 rgba 0", "", false)
	if response.Header.Get("X-GS-Cache") != "hit" || service.instances.Loads.Load() != 1 {
		t.Fatal("cache hit unnecessarily loaded GPU")
	}
	response, _ = callService(t, server, "/frame", "NGSREQ3 101 m-test 8 8 2 0 1 rgba 0", "", false)
	if response.StatusCode != 200 || service.instances.Loads.Load() != 2 {
		t.Fatal("unloaded instance did not reload")
	}
}
func TestLeasePinsIdleModelAndPredictionDoesNotRenew(t *testing.T) {
	service, server := testService(t)
	response, body := callService(t, server, "/sessions", "m-test", "", false)
	if response.StatusCode != 200 {
		t.Fatal("session create")
	}
	var result map[string]any
	json.Unmarshal(body, &result)
	session := result["session_id"].(string)
	callService(t, server, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0", session, false)
	service.instances.mutex.Lock()
	service.instances.loaded["m-test"].lastUser = time.Now().Add(-time.Hour)
	service.instances.mutex.Unlock()
	if err := service.maintain(); err != nil {
		t.Fatal(err)
	}
	if len(service.instances.status()) != 1 {
		t.Fatal("active lease lost instance")
	}
	before, _ := service.database.query("SELECT expires FROM sessions WHERE id=?", session)
	callService(t, server, "/frame", "NGSREQ3 2 m-test 8 8 2 0 1 rgba 0", session, true)
	after, _ := service.database.query("SELECT expires FROM sessions WHERE id=?", session)
	if before[0][0] != after[0][0] {
		t.Fatal("prediction renewed lease")
	}
	response, _ = callService(t, server, "/sessions/heartbeat", session, "", false)
	if response.StatusCode != 200 {
		t.Fatal("heartbeat")
	}
	response, _ = callService(t, server, "/sessions/close", session, "", false)
	if response.StatusCode != 200 {
		t.Fatal("close lease")
	}
	response, _ = callService(t, server, "/frame", "NGSREQ3 3 m-test 8 8 4 0 1 rgba 0", session, true)
	if response.StatusCode != 410 {
		t.Fatal("closed lease accepted")
	}
}
func TestAuthenticationAndPredictionRequiresLiveLease(t *testing.T) {
	service, server := testService(t)
	response, err := http.Get(server.URL + "/models")
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if response.StatusCode != 401 {
		t.Fatal("missing auth accepted")
	}
	_, body := callService(t, server, "/sessions", "m-test", "", false)
	var result map[string]any
	json.Unmarshal(body, &result)
	response, _ = callService(t, server, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0", result["session_id"].(string), true)
	if response.StatusCode != 200 || service.instances.Loads.Load() != 1 {
		t.Fatal("live viewer did not start prediction instance")
	}
	callService(t, server, "/sessions/close", result["session_id"].(string), "", false)
	response, _ = callService(t, server, "/frame", "NGSREQ3 2 m-test 8 8 2 0 1 rgba 0", result["session_id"].(string), true)
	if response.StatusCode != 410 {
		t.Fatal("closed viewer started prediction")
	}
}

func TestInstanceCapacityPriorityAndCanceledWaitRelease(t *testing.T) {
	service, server := testService(t)
	response, _ := callService(t, server, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0", "", false)
	if response.StatusCode != 200 {
		t.Fatal("initial frame failed")
	}
	manager := service.instances
	manager.max = 1
	other := manager.models["m-test"]
	other.ID = "m-other"
	manager.models[other.ID] = other
	if _, err := manager.acquire(context.Background(), other.ID, false); err == nil {
		t.Fatal("active instance capacity was exceeded")
	}
	device := manager.devices[0]
	device.gate <- struct{}{}
	ctx, cancel := context.WithCancel(context.Background())
	result := make(chan error, 1)
	view := View{ID: 2, Model: "m-test", Width: 8, Height: 8, Distance: 1, Profile: "rgba"}
	go func() {
		_, _, err := manager.render(ctx, view, false)
		result <- err
	}()
	deadline := time.Now().Add(time.Second)
	for device.foreground.Load() == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	if device.foreground.Load() != 1 {
		cancel()
		<-device.gate
		t.Fatal("foreground did not enter bounded GPU wait")
	}
	_, _, speculativeErr := manager.render(context.Background(), view, true)
	cancel()
	foregroundErr := <-result
	<-device.gate
	if speculativeErr == nil || foregroundErr != context.Canceled {
		t.Fatalf("priority/cancellation failed: %v %v", speculativeErr, foregroundErr)
	}
	manager.mutex.Lock()
	active := manager.loaded["m-test"].active
	manager.mutex.Unlock()
	if active != 0 || device.foreground.Load() != 0 || manager.Loads.Load() != 1 || manager.Renders.Load() != 1 {
		t.Fatal("canceled/speculative request leaked activity or rendered")
	}
}

func TestShutdownRejectsNewCacheMissProducers(t *testing.T) {
	service, server := testService(t)
	service.wait()
	response, _ := callService(t, server, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0", "", false)
	if response.StatusCode != 503 || service.instances.Loads.Load() != 0 {
		t.Fatal("shutdown admitted a new producer")
	}
}

func TestProducerBudgetRejectsMissButServesCacheHit(t *testing.T) {
	service, server := testService(t)
	response, _ := callService(t, server, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0", "", false)
	if response.StatusCode != 200 {
		t.Fatal("warmup failed")
	}
	for index := 0; index < cap(service.producerSlots); index++ {
		service.producerSlots <- struct{}{}
	}
	defer func() {
		for len(service.producerSlots) > 0 {
			<-service.producerSlots
		}
	}()
	response, _ = callService(t, server, "/frame", "NGSREQ3 2 m-test 8 8 2 0 1 rgba 0", "", false)
	if response.StatusCode != 503 {
		t.Fatal("producer budget bypassed")
	}
	response, _ = callService(t, server, "/frame", "NGSREQ3 3 m-test 8 8 0 0 1 rgba 0", "", false)
	if response.StatusCode != 200 || response.Header.Get("X-GS-Cache") != "hit" || service.instances.Renders.Load() != 1 {
		t.Fatal("producer saturation blocked cached frame or rendered again")
	}
}

func TestDatabaseRejectsUnknownSchema(t *testing.T) {
	path := filepath.Join(t.TempDir(), "metadata.sqlite")
	database, err := openDatabase(path)
	if err != nil {
		t.Fatal(err)
	}
	_, err = database.query("PRAGMA user_version=999")
	database.close()
	if err != nil {
		t.Fatal(err)
	}
	if reopened, err := openDatabase(path); err == nil {
		reopened.close()
		t.Fatal("unknown schema accepted")
	}
}
