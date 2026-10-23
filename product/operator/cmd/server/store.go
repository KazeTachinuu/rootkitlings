package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	_ "modernc.org/sqlite"
)

type Store struct {
	db *sql.DB
}

func OpenStore(dataDir string) (*Store, error) {
	if err := os.MkdirAll(dataDir, 0700); err != nil {
		return nil, err
	}
	dsn := filepath.Join(dataDir, "operator.db") + "?_busy_timeout=5000&_journal_mode=WAL&_foreign_keys=1"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, fmt.Errorf("open store: %w", err)
	}
	db.SetMaxOpenConns(1)
	s := &Store{db: db}
	if err := s.migrate(); err != nil {
		db.Close()
		return nil, fmt.Errorf("migrate: %w", err)
	}
	return s, nil
}

func (s *Store) Close() error { return s.db.Close() }

func (s *Store) migrate() error {
	stmts := []string{
		`CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY)`,
		`CREATE TABLE IF NOT EXISTS agents (
			id TEXT PRIMARY KEY,
			hostname TEXT,
			kernel TEXT,
			arch TEXT,
			pid INTEGER,
			uid INTEGER,
			distro_id TEXT,
			peer_addr TEXT,
			first_seen TEXT,
			last_seen TEXT
		)`,
		`CREATE TABLE IF NOT EXISTS audit_log (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			ts TEXT DEFAULT (datetime('now')),
			kind TEXT,
			agent TEXT,
			message TEXT,
			data TEXT
		)`,
		`CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(ts)`,
		`CREATE INDEX IF NOT EXISTS idx_audit_kind ON audit_log(kind)`,
		`CREATE TABLE IF NOT EXISTS labels (
			agent_id TEXT PRIMARY KEY,
			label TEXT
		)`,
	}
	for _, stmt := range stmts {
		if _, err := s.db.Exec(stmt); err != nil {
			return fmt.Errorf("exec %q: %w", stmt[:40], err)
		}
	}
	return nil
}

type AuditEntry struct {
	Time    string
	Kind    string
	Agent   string
	Message string
}

func (s *Store) InsertAudit(kind, agent, message string, data map[string]string) error {
	var dataJSON []byte
	if data != nil {
		var err error
		dataJSON, err = json.Marshal(data)
		if err != nil {
			return err
		}
	}
	_, err := s.db.Exec(`INSERT INTO audit_log (kind, agent, message, data) VALUES (?, ?, ?, ?)`,
		kind, agent, message, string(dataJSON))
	return err
}

func (s *Store) RecentAudit(n int) ([]AuditEntry, error) {
	rows, err := s.db.Query(`SELECT ts, kind, agent, message FROM audit_log ORDER BY id DESC LIMIT ?`, n)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var entries []AuditEntry
	for rows.Next() {
		var e AuditEntry
		if err := rows.Scan(&e.Time, &e.Kind, &e.Agent, &e.Message); err != nil {
			return nil, err
		}
		entries = append(entries, e)
	}
	return entries, rows.Err()
}

func (s *Store) PutLabel(agentID, label string) error {
	_, err := s.db.Exec(`
		INSERT INTO labels (agent_id, label) VALUES (?, ?)
		ON CONFLICT(agent_id) DO UPDATE SET label=excluded.label
	`, agentID, label)
	return err
}

func (s *Store) GetLabels() (map[string]string, error) {
	rows, err := s.db.Query(`SELECT agent_id, label FROM labels`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	labels := make(map[string]string)
	for rows.Next() {
		var id, label string
		if err := rows.Scan(&id, &label); err != nil {
			return nil, err
		}
		labels[id] = label
	}
	return labels, rows.Err()
}
