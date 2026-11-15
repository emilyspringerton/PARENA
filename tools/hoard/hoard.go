// hoard -- a small read-only web page over Fafnir's Bed: every character's bags, bank and mail, and every item
// across the account (who holds it, how many), in the Valhöll day style (WOTAN plates III, IV, VIII).
//
// WoW writes SavedVariables only on /reload and logout, so the page can never be fresher than the last /reload.
// It re-reads the file on every request; the page checks every few seconds and redraws when the file changes.
//
// Env (the same names scripts/collection-env.sh exports):
//
//	WOW_WTF_DIR    the client's WTF folder (required; no default)
//	FAFNIR_ADDON   Fafnir's Bed folder name, which names the SavedVariables file (default FafnirsBed)
//	FAFNIR_VAR     its global (default FafnirsBedDB)
//	HOARD_ADDR     listen address (default 127.0.0.1:8765). It is your play data: anything that is not a
//	               loopback address is refused unless HOARD_ALLOW_LAN=1.
package main

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

//go:embed page.html
var pageHTML []byte

type Config struct {
	WTF, Addon, Var string
}

type Stack struct {
	ID    int    `json:"id"`
	Name  string `json:"name"`
	Count int    `json:"count"`
}

type Character struct {
	Key      string  `json:"key"`
	Name     string  `json:"name"`
	Realm    string  `json:"realm"`
	Account  string  `json:"account"`
	Seen     int64   `json:"seen"`
	Copper   int64   `json:"copper"`
	Bags     []Stack `json:"bags"`
	Bank     []Stack `json:"bank"`
	Mail     []Stack `json:"mail"`
	BankRead bool    `json:"bankRead"`
	MailRead bool    `json:"mailRead"`
	Items    int     `json:"items"`
	Distinct int     `json:"distinct"`
}

type Holder struct {
	Character string `json:"character"`
	Where     string `json:"where"`
	Count     int    `json:"count"`
}

type Item struct {
	ID      int      `json:"id"`
	Name    string   `json:"name"`
	Total   int      `json:"total"`
	Holders []Holder `json:"holders"`
}

type Hoard struct {
	SavedAt    int64       `json:"savedAt"`
	Files      []string    `json:"files"`
	Characters []Character `json:"characters"`
	Items      []Item      `json:"items"`
	Copper     int64       `json:"copper"`
	Problems   []string    `json:"problems"`
}

func configFromEnv() (Config, error) {
	c := Config{WTF: os.Getenv("WOW_WTF_DIR"), Addon: os.Getenv("FAFNIR_ADDON"), Var: os.Getenv("FAFNIR_VAR")}
	if c.WTF == "" {
		return c, errors.New("WOW_WTF_DIR is not set: point it at the client's WTF folder (source scripts/collection-env.sh in MONOREPO)")
	}
	if c.Addon == "" {
		c.Addon = "FafnirsBed"
	}
	if c.Var == "" {
		c.Var = "FafnirsBedDB"
	}
	return c, nil
}

// Load reads every account's Fafnir's Bed file under the WTF folder.
func Load(c Config) (*Hoard, error) {
	files, err := filepath.Glob(filepath.Join(c.WTF, "Account", "*", "SavedVariables", c.Addon+".lua"))
	if err != nil {
		return nil, err
	}
	if len(files) == 0 {
		return nil, fmt.Errorf("no %s.lua under %s: log in once with Fafnir's Bed enabled, then /reload", c.Addon, filepath.Join(c.WTF, "Account", "*", "SavedVariables"))
	}
	sort.Strings(files)
	h := &Hoard{Characters: []Character{}, Items: []Item{}, Problems: []string{}}
	names := map[int]string{}
	for _, f := range files {
		account := filepath.Base(filepath.Dir(filepath.Dir(f)))
		st, err := os.Stat(f)
		if err != nil {
			return nil, err
		}
		if t := st.ModTime().Unix(); t > h.SavedAt {
			h.SavedAt = t
		}
		h.Files = append(h.Files, f)
		raw, err := os.ReadFile(f)
		if err != nil {
			return nil, err
		}
		vars, err := ParseSavedVariables(string(raw))
		if err != nil {
			h.Problems = append(h.Problems, fmt.Sprintf("%s: %v", f, err))
			continue
		}
		db, _ := vars[c.Var].(*Table)
		if db == nil {
			h.Problems = append(h.Problems, fmt.Sprintf("%s holds no %s table", f, c.Var))
			continue
		}
		if nt := db.Sub("names"); nt != nil {
			for _, k := range nt.Keys {
				if id, err := strconv.Atoi(k); err == nil {
					if s, ok := nt.Field[k].(string); ok {
						names[id] = s
					}
				}
			}
		}
		chars := db.Sub("chars")
		if chars == nil {
			continue
		}
		for _, key := range chars.Keys {
			ct, _ := chars.Field[key].(*Table)
			if ct == nil {
				continue
			}
			ch := Character{Key: key, Account: account, Seen: int64(ct.Num("seen")), Copper: int64(ct.Num("gold"))}
			ch.Name, ch.Realm = key, ""
			if i := strings.Index(key, "-"); i > 0 {
				ch.Name, ch.Realm = key[:i], key[i+1:]
			}
			loc := ct.Sub("loc")
			ch.Bags = stacks(loc.Sub("bags"))
			if b := loc.Sub("bank"); b != nil {
				ch.Bank, ch.BankRead = stacks(b), true
			}
			if m := loc.Sub("mail"); m != nil {
				ch.Mail, ch.MailRead = stacks(m), true
			}
			h.Characters = append(h.Characters, ch)
		}
	}
	byID := map[int]*Item{}
	for ci := range h.Characters {
		ch := &h.Characters[ci]
		seen := map[int]bool{}
		for _, part := range []struct {
			where string
			list  []Stack
		}{{"bags", ch.Bags}, {"bank", ch.Bank}, {"mail", ch.Mail}} {
			for si := range part.list {
				s := &part.list[si]
				s.Name = names[s.ID]
				ch.Items += s.Count
				seen[s.ID] = true
				it := byID[s.ID]
				if it == nil {
					it = &Item{ID: s.ID, Name: s.Name}
					byID[s.ID] = it
				}
				it.Total += s.Count
				it.Holders = append(it.Holders, Holder{Character: ch.Name, Where: part.where, Count: s.Count})
			}
		}
		ch.Distinct = len(seen)
		h.Copper += ch.Copper
		sortStacks(ch.Bags)
		sortStacks(ch.Bank)
		sortStacks(ch.Mail)
	}
	for _, it := range byID {
		sort.SliceStable(it.Holders, func(a, b int) bool { return it.Holders[a].Count > it.Holders[b].Count })
		h.Items = append(h.Items, *it)
	}
	sort.Slice(h.Items, func(a, b int) bool {
		if h.Items[a].Total != h.Items[b].Total {
			return h.Items[a].Total > h.Items[b].Total
		}
		return h.Items[a].ID < h.Items[b].ID
	})
	sort.SliceStable(h.Characters, func(a, b int) bool { return h.Characters[a].Seen > h.Characters[b].Seen })
	return h, nil
}

func stacks(t *Table) []Stack {
	out := []Stack{}
	if t == nil {
		return out
	}
	for _, k := range t.Keys {
		id, err := strconv.Atoi(k)
		if err != nil {
			continue
		}
		if n, ok := t.Field[k].(float64); ok && n > 0 {
			out = append(out, Stack{ID: id, Count: int(n)})
		}
	}
	return out
}

func sortStacks(s []Stack) {
	sort.Slice(s, func(a, b int) bool {
		if s[a].Count != s[b].Count {
			return s[a].Count > s[b].Count
		}
		return s[a].ID < s[b].ID
	})
}

func handler(c Config) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Cache-Control", "no-store")
		w.Write(pageHTML)
	})
	mux.HandleFunc("/api/hoard", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		w.Header().Set("Cache-Control", "no-store")
		h, err := Load(c)
		if err != nil {
			w.WriteHeader(http.StatusServiceUnavailable)
			json.NewEncoder(w).Encode(map[string]string{"error": err.Error()})
			return
		}
		json.NewEncoder(w).Encode(h)
	})
	return mux
}

func loopback(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		return false
	}
	if host == "localhost" {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func main() {
	c, err := configFromEnv()
	if err != nil {
		log.Fatal("hoard: ", err)
	}
	addr := os.Getenv("HOARD_ADDR")
	if addr == "" {
		addr = "127.0.0.1:8765"
	}
	if !loopback(addr) && os.Getenv("HOARD_ALLOW_LAN") != "1" {
		log.Fatalf("hoard: %s is not a loopback address; this page is your play data. Set HOARD_ALLOW_LAN=1 to serve it anyway.", addr)
	}
	if h, err := Load(c); err != nil {
		log.Printf("hoard: %v (the page will say so until the file appears)", err)
	} else {
		log.Printf("hoard: %d characters, %d kinds of item, from %d file(s)", len(h.Characters), len(h.Items), len(h.Files))
	}
	srv := &http.Server{Addr: addr, Handler: handler(c), ReadHeaderTimeout: 5 * time.Second}
	log.Printf("hoard: http://%s/  (WoW saves on /reload and logout; the page follows)", addr)
	log.Fatal(srv.ListenAndServe())
}
