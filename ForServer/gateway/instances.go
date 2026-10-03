package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

type Model struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Bytes    int64  `json:"bytes"`
	Revision string `json:"revision"`
	Path     string `json:"-"`
}

func fileDigest(path string) (string, error) {
	input, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer input.Close()
	hash := sha256.New()
	if _, err = io.Copy(hash, input); err != nil {
		return "", err
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}
func catalog(root string) (map[string]Model, error) {
	root, err := filepath.EvalSymlinks(root)
	if err != nil {
		return nil, err
	}
	result := make(map[string]Model)
	err = filepath.WalkDir(root, func(path string, entry os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if entry.IsDir() {
			return nil
		}
		extension := strings.ToLower(filepath.Ext(path))
		if extension != ".ply" && extension != ".spz" {
			return nil
		}
		resolved, err := filepath.EvalSymlinks(path)
		if err != nil {
			return err
		}
		relative, err := filepath.Rel(root, resolved)
		if err != nil || relative == ".." || strings.HasPrefix(relative, ".."+string(filepath.Separator)) {
			return fmt.Errorf("model path escapes catalog")
		}
		name, _ := filepath.Rel(root, path)
		name = filepath.ToSlash(name)
		if strings.ContainsAny(resolved, "\t\n\r") || len(result) >= 10000 {
			return fmt.Errorf("invalid catalog path/budget")
		}
		info, err := os.Stat(resolved)
		if err != nil || !info.Mode().IsRegular() {
			return fmt.Errorf("model is not a regular file")
		}
		revision, err := fileDigest(resolved)
		if err != nil {
			return err
		}
		resultID := digest([]byte(name))[:16]
		result["m-"+resultID] = Model{ID: "m-" + resultID, Name: name, Bytes: info.Size(), Revision: revision, Path: resolved}
		return nil
	})
	if err != nil {
		return nil, err
	}
	if len(result) == 0 {
		return nil, fmt.Errorf("empty model catalog")
	}
	return result, nil
}

type Instance struct {
	model         Model
	device, port  int
	process       *exec.Cmd
	ready, exited chan struct{}
	failure       error
	active        int
	lastUser      time.Time
	closing       bool
}
type Device struct {
	gate       chan struct{}
	foreground atomic.Int64
}
type Instances struct {
	mutex                      sync.Mutex
	models                     map[string]Model
	loaded                     map[string]*Instance
	devices                    map[int]*Device
	deviceIDs                  []int
	server, state, catalogPath string
	max, basePort              int
	idle                       time.Duration
	now                        func() time.Time
	Loads, Unloads, Renders    atomic.Int64
	http                       *http.Client
}

func newInstances(models map[string]Model, server, state string, devices []int, max, port int, idle time.Duration) (*Instances, error) {
	manager := &Instances{models: models, loaded: make(map[string]*Instance), devices: make(map[int]*Device), deviceIDs: devices, server: server, state: state, max: max, basePort: port, idle: idle, now: time.Now, http: &http.Client{Timeout: 180 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}}
	for _, id := range devices {
		manager.devices[id] = &Device{gate: make(chan struct{}, 1)}
	}
	manager.catalogPath = filepath.Join(state, "catalog-v3.tsv")
	var contents strings.Builder
	for _, model := range models {
		fmt.Fprintf(&contents, "%s\t%s\n", model.ID, model.Path)
	}
	if err := os.WriteFile(manager.catalogPath, []byte(contents.String()), 0600); err != nil {
		return nil, err
	}
	return manager, nil
}
func (manager *Instances) touch(model string) {
	manager.mutex.Lock()
	defer manager.mutex.Unlock()
	if instance := manager.loaded[model]; instance != nil {
		instance.lastUser = manager.now()
	}
}
func (manager *Instances) acquire(ctx context.Context, model string, speculative bool) (*Instance, error) {
	manager.mutex.Lock()
	instance := manager.loaded[model]
	fresh := false
	if instance == nil {
		if len(manager.loaded) >= manager.max {
			manager.mutex.Unlock()
			return nil, fmt.Errorf("instance capacity")
		}
		used := make(map[int]bool)
		for _, current := range manager.loaded {
			used[current.port] = true
		}
		port := manager.basePort
		for used[port] {
			port++
		}
		occupancy := make(map[int]int)
		for _, loaded := range manager.loaded {
			occupancy[loaded.device]++
		}
		device := manager.deviceIDs[0]
		for _, candidate := range manager.deviceIDs {
			if occupancy[candidate] < occupancy[device] {
				device = candidate
			}
		}
		instance = &Instance{model: manager.models[model], device: device, port: port, ready: make(chan struct{}), exited: make(chan struct{}), lastUser: manager.now()}
		manager.loaded[model] = instance
		fresh = true
	}
	if instance.closing {
		manager.mutex.Unlock()
		return nil, fmt.Errorf("instance unloading")
	}
	instance.active++
	if !speculative {
		instance.lastUser = manager.now()
	}
	manager.mutex.Unlock()
	if fresh {
		failure := manager.start(instance)
		manager.mutex.Lock()
		instance.failure = failure
		close(instance.ready)
		manager.mutex.Unlock()
		if failure != nil {
			manager.terminate(instance)
			manager.mutex.Lock()
			delete(manager.loaded, model)
			manager.mutex.Unlock()
		}
	}
	select {
	case <-ctx.Done():
		manager.release(instance)
		return nil, ctx.Err()
	case <-instance.ready:
	}
	manager.mutex.Lock()
	failure := instance.failure
	manager.mutex.Unlock()
	if failure != nil {
		manager.release(instance)
		return nil, failure
	}
	select {
	case <-instance.exited:
		manager.release(instance)
		return nil, fmt.Errorf("worker exited")
	default:
	}
	return instance, nil
}
func (manager *Instances) release(instance *Instance) {
	manager.mutex.Lock()
	defer manager.mutex.Unlock()
	instance.active--
}
func (manager *Instances) start(instance *Instance) error {
	revision, err := fileDigest(instance.model.Path)
	if err != nil || revision != instance.model.Revision {
		return fmt.Errorf("model changed; restart catalog")
	}
	log, err := os.OpenFile(filepath.Join(manager.state, "instance-"+instance.model.ID+".log"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	defer log.Close()
	instance.process = exec.Command(manager.server, "serve", "--catalog", manager.catalogPath, "--device", strconv.Itoa(instance.device), "--port", strconv.Itoa(instance.port), "--instance-model", instance.model.ID)
	instance.process.Stdout = log
	instance.process.Stderr = log
	instance.process.SysProcAttr = &syscall.SysProcAttr{Pdeathsig: syscall.SIGTERM}
	if err = instance.process.Start(); err != nil {
		return err
	}
	go func() { instance.process.Wait(); close(instance.exited) }()
	client := &http.Client{Timeout: time.Second}
	deadline := time.Now().Add(20 * time.Second)
	for time.Now().Before(deadline) {
		response, err := client.Get(fmt.Sprintf("http://127.0.0.1:%d/health", instance.port))
		if err == nil {
			io.Copy(io.Discard, io.LimitReader(response.Body, 1024))
			response.Body.Close()
			if response.StatusCode == 200 && response.Header.Get("X-GS-Worker-Pid") == strconv.Itoa(instance.process.Process.Pid) {
				manager.Loads.Add(1)
				return nil
			}
			return fmt.Errorf("worker port owner mismatch")
		}
		select {
		case <-instance.exited:
			return fmt.Errorf("worker startup failed")
		case <-time.After(20 * time.Millisecond):
		}
	}
	return fmt.Errorf("worker startup deadline")
}
func (manager *Instances) terminate(instance *Instance) {
	if instance.process == nil || instance.process.Process == nil {
		return
	}
	instance.process.Process.Signal(syscall.SIGTERM)
	select {
	case <-instance.exited:
	case <-time.After(12 * time.Second):
		instance.process.Process.Kill()
		<-instance.exited
	}
}
func (manager *Instances) render(ctx context.Context, view View, speculative bool) ([]byte, string, error) {
	instance, err := manager.acquire(ctx, view.Model, speculative)
	if err != nil {
		return nil, "", err
	}
	defer manager.release(instance)
	device := manager.devices[instance.device]
	if speculative {
		if device.foreground.Load() > 0 {
			return nil, "", fmt.Errorf("foreground waiting")
		}
		select {
		case device.gate <- struct{}{}:
		default:
			return nil, "", fmt.Errorf("GPU busy; skip prediction")
		}
	} else {
		device.foreground.Add(1)
		select {
		case device.gate <- struct{}{}:
			device.foreground.Add(-1)
		case <-ctx.Done():
			device.foreground.Add(-1)
			return nil, "", ctx.Err()
		}
	}
	defer func() { <-device.gate }()
	manager.mutex.Lock()
	closing := instance.closing
	manager.mutex.Unlock()
	if closing {
		return nil, "", fmt.Errorf("instance closing")
	}
	request, err := http.NewRequestWithContext(ctx, "POST", fmt.Sprintf("http://127.0.0.1:%d/frame", instance.port), strings.NewReader(view.body()))
	if err != nil {
		return nil, "", err
	}
	response, err := manager.http.Do(request)
	if err != nil {
		manager.mutex.Lock()
		instance.closing = true
		manager.mutex.Unlock()
		manager.terminate(instance)
		manager.mutex.Lock()
		if manager.loaded[view.Model] == instance {
			delete(manager.loaded, view.Model)
		}
		manager.mutex.Unlock()
		return nil, "", err
	}
	defer response.Body.Close()
	if response.StatusCode != 200 || response.Header.Get("X-GS-Request-Id") != strconv.FormatUint(view.ID, 10) || response.Header.Get("X-GS-Model-Id") != view.Model {
		return nil, "", fmt.Errorf("native worker response rejected")
	}
	packet, err := io.ReadAll(io.LimitReader(response.Body, maxPacket+1))
	if err != nil || !validPacket(packet, view) {
		return nil, "", fmt.Errorf("invalid native image packet")
	}
	manager.Renders.Add(1)
	return packet, response.Header.Get("X-GS-Stats"), nil
}
func (manager *Instances) sweep(leases map[string]bool) {
	manager.mutex.Lock()
	var victims []*Instance
	for _, instance := range manager.loaded {
		if leases[instance.model.ID] {
			instance.lastUser = manager.now()
		}
		exited := false
		select {
		case <-instance.exited:
			exited = true
		default:
		}
		if !instance.closing && instance.active == 0 && (exited || !leases[instance.model.ID] && manager.now().Sub(instance.lastUser) >= manager.idle) {
			instance.closing = true
			victims = append(victims, instance)
		}
	}
	manager.mutex.Unlock()
	for _, instance := range victims {
		manager.terminate(instance)
		manager.mutex.Lock()
		if manager.loaded[instance.model.ID] == instance {
			delete(manager.loaded, instance.model.ID)
		}
		manager.mutex.Unlock()
		manager.Unloads.Add(1)
	}
}
func (manager *Instances) close() {
	manager.mutex.Lock()
	var all []*Instance
	for _, instance := range manager.loaded {
		all = append(all, instance)
	}
	manager.mutex.Unlock()
	for _, instance := range all {
		manager.terminate(instance)
	}
}
func (manager *Instances) status() []map[string]any {
	manager.mutex.Lock()
	defer manager.mutex.Unlock()
	result := make([]map[string]any, 0, len(manager.loaded))
	for _, instance := range manager.loaded {
		pid := 0
		select {
		case <-instance.ready:
			if instance.process != nil && instance.process.Process != nil {
				pid = instance.process.Process.Pid
			}
		default:
		}
		result = append(result, map[string]any{"model": instance.model.ID, "gpu": instance.device, "pid": pid, "active": instance.active, "closing": instance.closing, "last_user": instance.lastUser.UTC()})
	}
	return result
}
