package main

/*
#cgo LDFLAGS: -lsqlite3
#include <sqlite3.h>
#include <stdlib.h>
static int bind_text(sqlite3_stmt *statement, int index, const char *value) {
    return sqlite3_bind_text(statement, index, value, -1, SQLITE_TRANSIENT);
}
*/
import "C"

import (
	"fmt"
	"os"
	"sync"
	"unsafe"
)

type Database struct {
	mutex  sync.Mutex
	handle *C.sqlite3
}

func openDatabase(path string) (*Database, error) {
	database := &Database{}
	name := C.CString(path)
	defer C.free(unsafe.Pointer(name))
	if C.sqlite3_open_v2(name, &database.handle, C.SQLITE_OPEN_READWRITE|C.SQLITE_OPEN_CREATE|C.SQLITE_OPEN_FULLMUTEX, nil) != C.SQLITE_OK {
		if database.handle != nil {
			C.sqlite3_close(database.handle)
		}
		return nil, fmt.Errorf("cannot open metadata database")
	}
	C.sqlite3_busy_timeout(database.handle, 5000)
	if err := os.Chmod(path, 0600); err != nil {
		database.close()
		return nil, err
	}
	version, err := database.query("PRAGMA user_version")
	if err != nil || len(version) != 1 || (version[0][0] != "0" && version[0][0] != "1") {
		database.close()
		return nil, fmt.Errorf("unsupported metadata schema")
	}
	for _, statement := range []string{
		"PRAGMA journal_mode=WAL", "PRAGMA synchronous=NORMAL",
		"CREATE TABLE IF NOT EXISTS cache(key TEXT PRIMARY KEY, filename TEXT NOT NULL, digest TEXT NOT NULL, bytes INTEGER NOT NULL, last_hit INTEGER NOT NULL)",
		"CREATE INDEX IF NOT EXISTS cache_lru ON cache(last_hit)",
		"CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY, model TEXT NOT NULL, expires INTEGER NOT NULL)",
		"PRAGMA user_version=1",
	} {
		if _, err := database.query(statement); err != nil {
			database.close()
			return nil, err
		}
	}
	return database, nil
}
func (database *Database) close() {
	database.mutex.Lock()
	defer database.mutex.Unlock()
	C.sqlite3_close(database.handle)
}
func (database *Database) query(sql string, parameters ...string) ([][]string, error) {
	database.mutex.Lock()
	defer database.mutex.Unlock()
	query := C.CString(sql)
	defer C.free(unsafe.Pointer(query))
	var statement *C.sqlite3_stmt
	if C.sqlite3_prepare_v2(database.handle, query, -1, &statement, nil) != C.SQLITE_OK {
		return nil, fmt.Errorf("database prepare: %s", C.GoString(C.sqlite3_errmsg(database.handle)))
	}
	defer C.sqlite3_finalize(statement)
	for index, value := range parameters {
		argument := C.CString(value)
		status := C.bind_text(statement, C.int(index+1), argument)
		C.free(unsafe.Pointer(argument))
		if status != C.SQLITE_OK {
			return nil, fmt.Errorf("database binding failed")
		}
	}
	var rows [][]string
	for {
		status := C.sqlite3_step(statement)
		if status == C.SQLITE_DONE {
			return rows, nil
		}
		if status != C.SQLITE_ROW {
			return nil, fmt.Errorf("database step: %s", C.GoString(C.sqlite3_errmsg(database.handle)))
		}
		row := make([]string, int(C.sqlite3_column_count(statement)))
		for index := range row {
			row[index] = C.GoString((*C.char)(unsafe.Pointer(C.sqlite3_column_text(statement, C.int(index)))))
		}
		rows = append(rows, row)
	}
}
