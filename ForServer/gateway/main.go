package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"time"
)

func main() {
	if err := run(); err != nil {
		log.Print(err)
		os.Exit(1)
	}
}
func run() error {
	server := flag.String("server", "", "native worker binary")
	modelsRoot := flag.String("models", "", "read-only model directory")
	state := flag.String("state", "", "instance state directory")
	tokenPath := flag.String("token-file", "", "secret bearer file")
	deviceList := flag.String("devices", "0", "GPU candidates")
	enableMulti := flag.Bool("enable-multi-gpu", false, "explicitly enable multiple GPUs")
	bind := flag.String("bind", "127.0.0.1", "public bind address")
	port := flag.Int("port", 8888, "public port")
	workerPort := flag.Int("worker-port", 19000, "first instance port")
	maximum := flag.Int("max-instances", 4, "resident instance limit")
	idle := flag.Duration("instance-idle", 5*time.Minute, "idle model grace")
	cacheTTL := flag.Duration("cache-ttl", 24*time.Hour, "time since last foreground hit")
	cacheBytes := flag.Int64("cache-bytes", 10<<30, "disk packet budget")
	leaseTTL := flag.Duration("lease-ttl", 120*time.Second, "session heartbeat expiry")
	interval := flag.Duration("sweep-interval", 10*time.Second, "retention maintenance interval")
	private := flag.Bool("private-http", false, "explicit trusted-network plaintext mode")
	cert := flag.String("cert", "", "TLS certificate")
	key := flag.String("key", "", "TLS private key")
	flag.Parse()
	if *server == "" || *modelsRoot == "" || *state == "" || *tokenPath == "" || *maximum < 1 || *maximum > 16 || *port < 1 || *port > 65535 || *workerPort < 1024 || *workerPort+*maximum > 65535 || *idle <= 0 || *cacheTTL <= 0 || *leaseTTL <= 0 || *interval <= 0 || *cacheBytes < 1024 {
		return fmt.Errorf("invalid configuration")
	}
	if !*private && *cert == "" && *bind != "127.0.0.1" && *bind != "::1" {
		return fmt.Errorf("TLS or explicit private HTTP required")
	}
	var devices []int
	seen := make(map[int]bool)
	for _, value := range strings.Split(*deviceList, ",") {
		id, err := strconv.Atoi(value)
		if err != nil || id < 0 || id > 255 || seen[id] {
			return fmt.Errorf("invalid devices")
		}
		seen[id] = true
		devices = append(devices, id)
	}
	if len(devices) > 1 && !*enableMulti {
		return fmt.Errorf("multiple GPUs are disabled; set --enable-multi-gpu explicitly")
	}
	available, err := exec.Command(*server, "devices").Output()
	if err != nil {
		return fmt.Errorf("GPU discovery failed: %w", err)
	}
	resolvedServer, err := filepath.EvalSymlinks(*server)
	if err != nil {
		return err
	}
	*server = resolvedServer
	detected := make(map[int]bool)
	for _, line := range strings.Split(string(available), "\n") {
		fields := strings.Fields(line)
		if len(fields) > 0 {
			id, parseErr := strconv.Atoi(fields[0])
			if parseErr == nil {
				detected[id] = true
			}
		}
	}
	for _, device := range devices {
		if !detected[device] {
			return fmt.Errorf("configured GPU %d is unavailable", device)
		}
	}
	if err := os.MkdirAll(*state, 0700); err != nil {
		return err
	}
	lock, err := os.OpenFile(filepath.Join(*state, "gateway-v3.lock"), os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return err
	}
	defer lock.Close()
	if err = syscall.Flock(int(lock.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		return fmt.Errorf("gateway state is already owned")
	}
	tokenBytes, err := os.ReadFile(*tokenPath)
	if os.IsNotExist(err) {
		token, failure := randomToken()
		if failure != nil {
			return failure
		}
		err = os.WriteFile(*tokenPath, []byte(token+"\n"), 0600)
		tokenBytes = []byte(token)
	}
	if err != nil {
		return err
	}
	token := strings.TrimSpace(string(tokenBytes))
	if len(token) < 32 || len(token) > 256 || strings.ContainsAny(token, "\r\n\t ") {
		return fmt.Errorf("invalid access token")
	}
	models, err := catalog(*modelsRoot)
	if err != nil {
		return err
	}
	renderer, err := fileDigest(*server)
	if err != nil {
		return err
	}
	database, err := openDatabase(filepath.Join(*state, "metadata.sqlite"))
	if err != nil {
		return err
	}
	defer database.close()
	if _, err = database.query("DELETE FROM sessions"); err != nil {
		return err
	}
	cache, err := newCache(database, filepath.Join(*state, "frames"), *cacheTTL, *cacheBytes)
	if err != nil {
		return err
	}
	instances, err := newInstances(models, *server, *state, devices, *maximum, *workerPort, *idle)
	if err != nil {
		return err
	}
	defer instances.close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	service := newService(ctx, models, instances, cache, renderer, token, *leaseTTL)
	httpServer := &http.Server{Addr: net.JoinHostPort(*bind, strconv.Itoa(*port)), Handler: service, ReadHeaderTimeout: 5 * time.Second, ReadTimeout: 10 * time.Second, WriteTimeout: 200 * time.Second, IdleTimeout: 10 * time.Second, MaxHeaderBytes: 8192}
	var connections atomic.Int64
	httpServer.ConnState = func(connection net.Conn, state http.ConnState) {
		switch state {
		case http.StateNew:
			if connections.Add(1) > 128 {
				connection.Close()
			}
		case http.StateClosed:
			connections.Add(-1)
		}
	}
	failures := make(chan error, 1)
	go func() {
		if *cert != "" {
			failures <- httpServer.ListenAndServeTLS(*cert, *key)
		} else {
			failures <- httpServer.ListenAndServe()
		}
	}()
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGTERM, syscall.SIGINT)
	defer signal.Stop(signals)
	ticker := time.NewTicker(*interval)
	defer ticker.Stop()
	log.Printf("gateway api=3 devices=%v max_instances=%d", devices, *maximum)
	for {
		select {
		case failure := <-failures:
			cancel()
			service.wait()
			return failure
		case <-signals:
			cancel()
			shutdown, done := context.WithTimeout(context.Background(), 15*time.Second)
			defer done()
			httpServer.Shutdown(shutdown)
			service.wait()
			return nil
		case <-ticker.C:
			if err = service.maintain(); err != nil {
				log.Printf("maintenance: %v", err)
			}
		}
	}
}
