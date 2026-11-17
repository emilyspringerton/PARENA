// hoard -- a small read-only web page over Fafnir's Bed: every character's equipped gear, bags, bank and mail, and
// every item across the account (who holds it, how many), in the Valhöll day style (WOTAN plates III, IV, VIII).
//
//	hoard              serve the page (and /api/hoard.csv) on HOARD_ADDR
//	hoard -csv FILE    write the CSV (csv.go) to FILE, or to stdout for "-", and exit
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
	"flag"
	"fmt"
	"io"
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
	Equipped []Stack `json:"equipped"`
	// Gear is what is worn, slot by slot (from chars[key].slots). GearRead is false for characters not seen
	// since the addon learned to read equipment: their gear is unknown, not empty.
	Gear     []Worn `json:"gear"`
	GearRead bool   `json:"gearRead"`
	Items    int    `json:"items"`
	Distinct int    `json:"distinct"`
}

// Worn is one equipped slot.
type Worn struct {
	Slot     int    `json:"slot"`
	SlotName string `json:"slotName"`
	ID       int    `json:"id"`
	Name     string `json:"name"`
}

// slotNames are WoW's inventory slot ids (the INVSLOT_* globals of every 1.x-era client). An id outside this
// table is shown as "slot N" rather than guessed.
var slotNames = map[int]string{
	0: "Ammo", 1: "Head", 2: "Neck", 3: "Shoulder", 4: "Shirt", 5: "Chest", 6: "Waist", 7: "Legs", 8: "Feet",
	9: "Wrist", 10: "Hands", 11: "Finger 1", 12: "Finger 2", 13: "Trinket 1", 14: "Trinket 2", 15: "Back",
	16: "Main hand", 17: "Off hand", 18: "Ranged", 19: "Tabard",
}

func slotName(slot int) string {
	if n, ok := slotNames[slot]; ok {
		return n
	}
	return "slot " + strconv.Itoa(slot)
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

// Parcel is mail in flight: sent by one of your characters, not yet seen in the recipient's mailbox.
type Parcel struct {
	To        string  `json:"to"`
	ToName    string  `json:"toName"`
	From      string  `json:"from"`
	FromName  string  `json:"fromName"`
	At        int64   `json:"at"`
	Copper    int64   `json:"copper"`
	Items     []Stack `json:"items"`
	OnAccount bool    `json:"onAccount"`
}

type Hoard struct {
	SavedAt    int64       `json:"savedAt"`
	Files      []string    `json:"files"`
	Characters []Character `json:"characters"`
	Items      []Item      `json:"items"`
	Post       []Parcel    `json:"post"`
	Copper     int64       `json:"copper"`
	Problems   []string    `json:"problems"`
}

func shortName(key string) string {
	if i := strings.Index(key, "-"); i > 0 {
		return key[:i]
	}
	return key
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
	h := &Hoard{Characters: []Character{}, Items: []Item{}, Post: []Parcel{}, Problems: []string{}}
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
		if post := db.Sub("post"); post != nil {
			for _, to := range post.Keys {
				box, _ := post.Field[to].(*Table)
				if box == nil {
					continue
				}
				for _, n := range box.Keys {
					pt, _ := box.Field[n].(*Table)
					if pt == nil {
						continue
					}
					from, _ := pt.Get("from").(string)
					p := Parcel{To: to, ToName: shortName(to), From: from, FromName: shortName(from), At: int64(pt.Num("at")),
						Copper: int64(pt.Num("copper")), Items: stacks(pt.Sub("items"))}
					if len(p.Items) > 0 || p.Copper > 0 {
						h.Post = append(h.Post, p)
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
			// never nil: a location never read is [] (with BankRead/MailRead false), not JSON null
			ch.Bags, ch.Bank, ch.Mail = stacks(loc.Sub("bags")), []Stack{}, []Stack{}
			if b := loc.Sub("bank"); b != nil {
				ch.Bank, ch.BankRead = stacks(b), true
			}
			if m := loc.Sub("mail"); m != nil {
				ch.Mail, ch.MailRead = stacks(m), true
			}
			ch.Equipped = stacks(loc.Sub("equipped"))
			ch.Gear = []Worn{}
			if st := ct.Sub("slots"); st != nil {
				ch.GearRead = true
				for _, k := range st.Keys {
					slot, err := strconv.Atoi(k)
					if err != nil {
						continue
					}
					if id, ok := st.Field[k].(float64); ok && id > 0 {
						ch.Gear = append(ch.Gear, Worn{Slot: slot, SlotName: slotName(slot), ID: int(id)})
					}
				}
				sort.Slice(ch.Gear, func(a, b int) bool { return ch.Gear[a].Slot < ch.Gear[b].Slot })
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
		}{{"equipped", ch.Equipped}, {"bags", ch.Bags}, {"bank", ch.Bank}, {"mail", ch.Mail}} {
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
		for gi := range ch.Gear {
			ch.Gear[gi].Name = names[ch.Gear[gi].ID]
		}
		sortStacks(ch.Equipped)
		sortStacks(ch.Bags)
		sortStacks(ch.Bank)
		sortStacks(ch.Mail)
	}
	// Parcels to your own characters still belong to the account: count them as "post", held by the recipient.
	// Mail to anyone else has left the account; it is listed, not counted.
	keys := map[string]bool{}
	for _, ch := range h.Characters {
		keys[ch.Key] = true
	}
	for pi := range h.Post {
		p := &h.Post[pi]
		p.OnAccount = keys[p.To]
		for si := range p.Items {
			s := &p.Items[si]
			s.Name = names[s.ID]
			if !p.OnAccount {
				continue
			}
			it := byID[s.ID]
			if it == nil {
				it = &Item{ID: s.ID, Name: s.Name}
				byID[s.ID] = it
			}
			it.Total += s.Count
			it.Holders = append(it.Holders, Holder{Character: p.ToName, Where: "post", Count: s.Count})
		}
		sortStacks(p.Items)
		if p.OnAccount {
			h.Copper += p.Copper
		}
	}
	sort.SliceStable(h.Post, func(a, b int) bool { return h.Post[a].At > h.Post[b].At })
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
	mux.HandleFunc("/api/hoard.csv", func(w http.ResponseWriter, r *http.Request) {
		h, err := Load(c)
		if err != nil {
			http.Error(w, err.Error(), http.StatusServiceUnavailable)
			return
		}
		w.Header().Set("Content-Type", "text/csv; charset=utf-8")
		w.Header().Set("Content-Disposition", `attachment; filename="hoard.csv"`)
		w.Header().Set("Cache-Control", "no-store")
		WriteCSV(w, h)
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
	csvOut := flag.String("csv", "", `write the hoard as CSV to this file ("-" for stdout) and exit`)
	flag.Parse()
	c, err := configFromEnv()
	if err != nil {
		log.Fatal("hoard: ", err)
	}
	if *csvOut != "" {
		h, err := Load(c)
		if err != nil {
			log.Fatal("hoard: ", err)
		}
		w := io.Writer(os.Stdout)
		if *csvOut != "-" {
			f, err := os.Create(*csvOut)
			if err != nil {
				log.Fatal("hoard: ", err)
			}
			defer f.Close()
			w = f
		}
		if err := WriteCSV(w, h); err != nil {
			log.Fatal("hoard: ", err)
		}
		log.Printf("hoard: %d rows for %d characters -> %s", len(Rows(h)), len(h.Characters), *csvOut)
		return
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
