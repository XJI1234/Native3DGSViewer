package main

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"hash/crc32"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

const maxPacket = (64 << 20) + 64

type Cache struct {
	mutex        sync.Mutex
	database     *Database
	root         string
	ttl          time.Duration
	budget       int64
	now          func() time.Time
	Hits, Misses atomic.Int64
}

func validPacket(packet []byte, view View) bool {
	if len(packet) < 64 || len(packet) > maxPacket || string(packet[:8]) != "NGSFRM02" {
		return false
	}
	header := append([]byte(nil), packet[:64]...)
	expected := binary.LittleEndian.Uint32(header[52:])
	clear(header[52:56])
	profile := uint32(0)
	if view.Profile != "rgba" {
		quality, _ := strconv.Atoi(view.Profile[4:])
		profile = uint32(quality)
	}
	compression := binary.LittleEndian.Uint32(packet[28:])
	return binary.LittleEndian.Uint32(packet[8:]) == 2 && binary.LittleEndian.Uint32(packet[12:]) == profile &&
		binary.LittleEndian.Uint32(packet[16:]) == uint32(view.Width) && binary.LittleEndian.Uint32(packet[20:]) == uint32(view.Height) &&
		binary.LittleEndian.Uint32(packet[24:]) == 0 && binary.LittleEndian.Uint64(packet[56:]) == 0 && compression <= 1 && (profile == 0 || compression == 0) &&
		binary.LittleEndian.Uint64(packet[32:]) == uint64(view.Width)*uint64(view.Height)*4 &&
		binary.LittleEndian.Uint64(packet[40:]) == uint64(len(packet)-64) && len(packet) > 64 &&
		(profile != 0 || compression != 0 || len(packet)-64 == view.Width*view.Height*4) &&
		crc32.ChecksumIEEE(header) == expected && crc32.ChecksumIEEE(packet[64:]) == binary.LittleEndian.Uint32(packet[48:])
}
func digest(packet []byte) string { sum := sha256.Sum256(packet); return hex.EncodeToString(sum[:]) }
func newCache(database *Database, root string, ttl time.Duration, budget int64) (*Cache, error) {
	if err := os.MkdirAll(root, 0700); err != nil {
		return nil, err
	}
	cache := &Cache{database: database, root: root, ttl: ttl, budget: budget, now: time.Now}
	rows, err := database.query("SELECT filename FROM cache")
	if err != nil {
		return nil, err
	}
	indexed := make(map[string]bool)
	for _, row := range rows {
		indexed[filepath.Clean(row[0])] = true
	}
	err = filepath.WalkDir(root, func(path string, entry os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if entry.IsDir() {
			return nil
		}
		relative, _ := filepath.Rel(root, path)
		if !indexed[relative] {
			return os.Remove(path)
		}
		return nil
	})
	return cache, err
}
func (cache *Cache) get(key string, view View, userHit bool) ([]byte, error) {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	rows, err := cache.database.query("SELECT filename,digest,last_hit FROM cache WHERE key=?", key)
	if err != nil {
		return nil, err
	}
	if len(rows) == 0 {
		cache.Misses.Add(1)
		return nil, nil
	}
	record := rows[0]
	last, _ := strconv.ParseInt(record[2], 10, 64)
	if cache.now().Sub(time.UnixMilli(last)) >= cache.ttl {
		cache.remove(key, record[0])
		cache.Misses.Add(1)
		return nil, nil
	}
	path := filepath.Join(cache.root, record[0])
	info, err := os.Stat(path)
	if err != nil || info.Size() > maxPacket || !info.Mode().IsRegular() {
		cache.remove(key, record[0])
		return nil, nil
	}
	packet, err := os.ReadFile(path)
	if err != nil || !validPacket(packet, view) || digest(packet) != record[1] {
		cache.remove(key, record[0])
		return nil, nil
	}
	if userHit {
		if _, err = cache.database.query("UPDATE cache SET last_hit=? WHERE key=?", strconv.FormatInt(cache.now().UnixMilli(), 10), key); err != nil {
			return nil, err
		}
		cache.Hits.Add(1)
	}
	return packet, nil
}
func (cache *Cache) remove(key, filename string) error {
	if _, err := cache.database.query("DELETE FROM cache WHERE key=?", key); err != nil {
		return err
	}
	err := os.Remove(filepath.Join(cache.root, filename))
	if os.IsNotExist(err) {
		return nil
	}
	return err
}
func (cache *Cache) put(key, filename string, packet []byte) error {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	if int64(len(packet)) > cache.budget {
		return nil
	}
	path := filepath.Join(cache.root, filename)
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	temporary, err := os.CreateTemp(filepath.Dir(path), ".frame-*")
	if err != nil {
		return err
	}
	defer os.Remove(temporary.Name())
	_, err = temporary.Write(packet)
	closeErr := temporary.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	if err = os.Rename(temporary.Name(), path); err != nil {
		return err
	}
	_, err = cache.database.query("INSERT OR REPLACE INTO cache(key,filename,digest,bytes,last_hit) VALUES(?,?,?,?,?)", key, filename, digest(packet), strconv.Itoa(len(packet)), strconv.FormatInt(cache.now().UnixMilli(), 10))
	if err != nil {
		os.Remove(path)
		return err
	}
	rows, err := cache.database.query("SELECT COUNT(*),COALESCE(SUM(bytes),0) FROM cache")
	if err != nil {
		return err
	}
	count, _ := strconv.ParseInt(rows[0][0], 10, 64)
	total, _ := strconv.ParseInt(rows[0][1], 10, 64)
	if total > cache.budget || count > 100000 {
		return cache.sweepLocked()
	}
	return nil
}
func (cache *Cache) sweepLocked() error {
	rows, err := cache.database.query("SELECT key,filename,bytes,last_hit FROM cache ORDER BY last_hit ASC,key ASC")
	if err != nil {
		return err
	}
	var total int64
	for _, row := range rows {
		size, _ := strconv.ParseInt(row[2], 10, 64)
		total += size
	}
	count := len(rows)
	for _, row := range rows {
		size, _ := strconv.ParseInt(row[2], 10, 64)
		last, _ := strconv.ParseInt(row[3], 10, 64)
		if total > cache.budget || count > 100000 || cache.now().Sub(time.UnixMilli(last)) >= cache.ttl {
			if err = cache.remove(row[0], row[1]); err != nil {
				return err
			}
			total -= size
			count--
		}
	}
	return nil
}
func (cache *Cache) sweep() error {
	cache.mutex.Lock()
	defer cache.mutex.Unlock()
	return cache.sweepLocked()
}
func cacheIdentity(model Model, renderer string, view View) (string, string) {
	filename := filepath.Join(model.ID, model.Revision, renderer, view.variant()+".ngsf")
	return fmt.Sprintf("sphere2-distance81-v1/%s", filename), filename
}
