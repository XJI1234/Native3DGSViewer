package main

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Flight struct {
	speculative bool
	done        chan struct{}
	packet      []byte
	stats       string
	failure     error
}
type Service struct {
	producerSlots   chan struct{}
	mutex           sync.Mutex
	flights         map[string]*Flight
	producer        sync.WaitGroup
	models          map[string]Model
	instances       *Instances
	cache           *Cache
	database        *Database
	renderer, token string
	slots           chan struct{}
	context         context.Context
	leaseTTL        time.Duration
	stopped         bool
}

func newService(ctx context.Context, models map[string]Model, instances *Instances, cache *Cache, renderer, token string, leaseTTL time.Duration) *Service {
	return &Service{flights: make(map[string]*Flight), models: models, instances: instances, cache: cache, database: cache.database, renderer: renderer, token: token, slots: make(chan struct{}, 64), producerSlots: make(chan struct{}, 64), context: ctx, leaseTTL: leaseTTL}
}
func jsonResponse(writer http.ResponseWriter, value any) {
	writer.Header().Set("Content-Type", "application/json")
	json.NewEncoder(writer).Encode(value)
}
func reject(writer http.ResponseWriter, status int, code string) {
	writer.Header().Set("Content-Type", "application/json")
	writer.WriteHeader(status)
	json.NewEncoder(writer).Encode(map[string]string{"error": code})
}
func (service *Service) ServeHTTP(writer http.ResponseWriter, request *http.Request) {
	writer.Header().Set("Cache-Control", "no-store")
	if len(request.TransferEncoding) > 0 || request.ContentLength > 1024 {
		reject(writer, 400, "invalid_body")
		return
	}
	for _, values := range request.Header {
		if len(values) > 1 {
			reject(writer, 400, "duplicate_header")
			return
		}
	}
	select {
	case service.slots <- struct{}{}:
		defer func() { <-service.slots }()
	default:
		reject(writer, 503, "connection_budget")
		return
	}
	if request.URL.Path == "/health" && request.Method == "GET" {
		jsonResponse(writer, map[string]any{"protocol": 2, "api_version": 3, "status": "ready", "workers": len(service.instances.status()), "available": len(service.instances.deviceIDs)})
		return
	}
	if subtle.ConstantTimeCompare([]byte(request.Header.Get("Authorization")), []byte("Bearer "+service.token)) != 1 {
		reject(writer, 401, "unauthorized")
		return
	}
	if request.Method == "GET" {
		switch request.URL.Path {
		case "/models":
			models := make([]Model, 0, len(service.models))
			for _, model := range service.models {
				models = append(models, model)
			}
			sort.Slice(models, func(left, right int) bool { return models[left].Name < models[right].Name })
			jsonResponse(writer, models)
		case "/status":
			rows, err := service.database.query("SELECT COUNT(*),COALESCE(SUM(bytes),0) FROM cache")
			if err != nil {
				reject(writer, 500, "metadata_failure")
				return
			}
			jsonResponse(writer, map[string]any{"api_version": 3, "devices": service.instances.deviceIDs, "multi_gpu": len(service.instances.deviceIDs) > 1, "instances": service.instances.status(), "loads": service.instances.Loads.Load(), "unloads": service.instances.Unloads.Load(), "renders": service.instances.Renders.Load(), "cache_hits": service.cache.Hits.Load(), "cache_misses": service.cache.Misses.Load(), "cache_entries": rows[0][0], "cache_bytes": rows[0][1]})
		default:
			reject(writer, 404, "not_found")
		}
		return
	}
	if request.Method != "POST" {
		reject(writer, 405, "method_not_allowed")
		return
	}
	body, err := io.ReadAll(io.LimitReader(request.Body, 1025))
	if err != nil || len(body) > 1024 {
		reject(writer, 400, "invalid_body")
		return
	}
	switch request.URL.Path {
	case "/sessions", "/sessions/heartbeat", "/sessions/close":
		service.session(writer, request.URL.Path, strings.TrimSpace(string(body)))
	case "/frame":
		service.frame(writer, request, string(body))
	default:
		reject(writer, 404, "not_found")
	}
}
func (service *Service) session(writer http.ResponseWriter, path, body string) {
	service.mutex.Lock()
	defer service.mutex.Unlock()
	now := time.Now().UnixMilli()
	if path == "/sessions" {
		if _, exists := service.models[body]; !exists {
			reject(writer, 404, "unknown_model")
			return
		}
		if _, err := service.database.query("DELETE FROM sessions WHERE expires<=?", strconv.FormatInt(now, 10)); err != nil {
			reject(writer, 500, "metadata_failure")
			return
		}
		rows, err := service.database.query("SELECT COUNT(*) FROM sessions")
		if err != nil {
			reject(writer, 500, "metadata_failure")
			return
		}
		count, _ := strconv.Atoi(rows[0][0])
		if count >= 256 {
			reject(writer, 503, "session_capacity")
			return
		}
		entropy := make([]byte, 24)
		if _, err = rand.Read(entropy); err != nil {
			reject(writer, 500, "entropy_failure")
			return
		}
		id := hex.EncodeToString(entropy)
		if _, err = service.database.query("INSERT INTO sessions(id,model,expires) VALUES(?,?,?)", id, body, strconv.FormatInt(now+service.leaseTTL.Milliseconds(), 10)); err != nil {
			reject(writer, 500, "metadata_failure")
			return
		}
		service.instances.touch(body)
		jsonResponse(writer, map[string]any{"session_id": id, "expires_in_ms": service.leaseTTL.Milliseconds()})
		return
	}
	rows, err := service.database.query("SELECT model,expires FROM sessions WHERE id=?", body)
	if err != nil {
		reject(writer, 500, "metadata_failure")
		return
	}
	if len(rows) == 0 {
		reject(writer, 410, "session_expired")
		return
	}
	expires, _ := strconv.ParseInt(rows[0][1], 10, 64)
	if path == "/sessions/close" {
		_, err = service.database.query("DELETE FROM sessions WHERE id=?", body)
	} else {
		if expires <= now {
			reject(writer, 410, "session_expired")
			return
		}
		_, err = service.database.query("UPDATE sessions SET expires=? WHERE id=?", strconv.FormatInt(now+service.leaseTTL.Milliseconds(), 10), body)
	}
	if err != nil {
		reject(writer, 500, "metadata_failure")
		return
	}
	service.instances.touch(rows[0][0])
	jsonResponse(writer, map[string]bool{"ok": true})
}
func (service *Service) frame(writer http.ResponseWriter, request *http.Request, body string) {
	view, err := parseView(body)
	if err != nil {
		reject(writer, 400, "invalid_view")
		return
	}
	model, exists := service.models[view.Model]
	if !exists {
		reject(writer, 404, "unknown_model")
		return
	}
	speculative := request.Header.Get("X-GS-Prefetch") == "1"
	session := request.Header.Get("X-GS-Session")
	if speculative && session == "" {
		reject(writer, 400, "prefetch_requires_session")
		return
	}
	if session != "" {
		rows, err := service.database.query("SELECT model,expires FROM sessions WHERE id=?", session)
		if err != nil {
			reject(writer, 500, "metadata_failure")
			return
		}
		if len(rows) == 0 {
			reject(writer, 410, "session_expired")
			return
		}
		expires, _ := strconv.ParseInt(rows[0][1], 10, 64)
		if rows[0][0] != view.Model || expires <= time.Now().UnixMilli() {
			reject(writer, 410, "session_model_expired")
			return
		}
	}
	if !speculative {
		service.instances.touch(view.Model)
	}
	key, filename := cacheIdentity(model, service.renderer, view)
	start := time.Now()
	packet, err := service.cache.get(key, view, !speculative)
	if err != nil {
		reject(writer, 500, "cache_failure")
		return
	}
	state := "hit"
	stats := "{}"
	retriedPrediction := false
	for packet == nil {
		service.mutex.Lock()
		if service.stopped {
			service.mutex.Unlock()
			reject(writer, 503, "service_stopping")
			return
		}
		flight := service.flights[key]
		if flight == nil {
			existing, lookupErr := service.cache.get(key, view, !speculative)
			if lookupErr != nil {
				service.mutex.Unlock()
				reject(writer, 500, "cache_failure")
				return
			}
			if existing != nil {
				flight = &Flight{done: make(chan struct{}), packet: existing, stats: "{}"}
				close(flight.done)
				state = "hit"
			} else {
				select {
				case service.producerSlots <- struct{}{}:
				default:
					service.mutex.Unlock()
					reject(writer, 503, "render_queue_capacity")
					return
				}
				flight = &Flight{done: make(chan struct{}), speculative: speculative}
				service.flights[key] = flight
				state = "miss"
				service.producer.Add(1)
				go func() {
					defer service.producer.Done()
					defer func() { <-service.producerSlots }()
					ctx, cancel := context.WithTimeout(service.context, 180*time.Second)
					defer cancel()
					flight.packet, flight.stats, flight.failure = service.instances.render(ctx, view, speculative)
					if flight.failure == nil {
						flight.failure = service.cache.put(key, filename, flight.packet)
					}
					service.mutex.Lock()
					delete(service.flights, key)
					close(flight.done)
					service.mutex.Unlock()
				}()
			}
		} else {
			state = "shared"
		}
		service.mutex.Unlock()
		select {
		case <-request.Context().Done():
			return
		case <-flight.done:
		}
		if flight.failure != nil {
			if !speculative && flight.speculative && !retriedPrediction {
				retriedPrediction = true
				continue
			}
			log.Printf("render failed model=%s: %v", view.Model, flight.failure)
			reject(writer, 503, "render_unavailable")
			return
		}
		packet, stats = flight.packet, flight.stats
	}
	writer.Header().Set("Content-Type", "application/x-native3dgs-frame")
	writer.Header().Set("Content-Length", strconv.Itoa(len(packet)))
	writer.Header().Set("X-GS-Request-Id", strconv.FormatUint(view.ID, 10))
	writer.Header().Set("X-GS-Model-Id", view.Model)
	writer.Header().Set("X-GS-View-Code", view.code())
	writer.Header().Set("X-GS-Cache", state)
	writer.Header().Set("X-GS-Stats", stats)
	writer.Write(packet)
	if !speculative {
		log.Printf("frame model=%s view=%s cache=%s bytes=%d ms=%.3f", view.Model, view.variant(), state, len(packet), float64(time.Since(start).Microseconds())/1000)
	}
}
func (service *Service) maintain() error {
	now := strconv.FormatInt(time.Now().UnixMilli(), 10)
	if _, err := service.database.query("DELETE FROM sessions WHERE expires<=?", now); err != nil {
		return err
	}
	rows, err := service.database.query("SELECT DISTINCT model FROM sessions WHERE expires>?", now)
	if err != nil {
		return err
	}
	active := make(map[string]bool)
	for _, row := range rows {
		active[row[0]] = true
	}
	service.instances.sweep(active)
	return service.cache.sweep()
}
func (service *Service) wait() {
	service.mutex.Lock()
	service.stopped = true
	service.mutex.Unlock()
	service.producer.Wait()
}
func randomToken() (string, error) {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	return fmt.Sprintf("%x", bytes), nil
}
