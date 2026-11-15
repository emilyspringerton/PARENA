package main

// Made-up data only (D-8): the founder's real SavedVariables never enter this repo.

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const fakeSV = `
-- a comment the client never writes, but a reader should not choke on
FafnirsBedDB = {
["v"] = 1,
["names"] = {
[2589] = "Linen Cloth",
[6948] = "Hearthstone",
[159] = "Refreshing \"Spring\" Water",
},
["chars"] = {
["Astrid-Test Realm"] = {
["seen"] = 1794738458,
["loc"] = {
["bags"] = {
[2589] = 16,
[6948] = 1,
[999999] = 2,
},
["mail"] = {
},
["bank"] = {
[2589] = 4,
},
},
["gold"] = 12345,
},
["Bjorn-Test Realm"] = {
["seen"] = 1794700000,
["loc"] = {
["bags"] = {
[6948] = 1,
[159] = 3,
[2589] = 0,
},
},
["gold"] = 289,
},
},
}
OtherAddonDB = { "a", 'b', true, false, nil, -1.5e2, 0x10, { nested = { 1, 2; 3 } } }
`

func TestParseSavedVariables(t *testing.T) {
	vars, err := ParseSavedVariables(fakeSV)
	if err != nil {
		t.Fatal(err)
	}
	db := vars["FafnirsBedDB"].(*Table)
	if db.Num("v") != 1 {
		t.Fatalf("v = %v", db.Get("v"))
	}
	if got := db.Sub("names").Get("159"); got != `Refreshing "Spring" Water` {
		t.Fatalf("escaped string = %q", got)
	}
	other := vars["OtherAddonDB"].(*Table)
	if len(other.List) != 8 || other.List[5] != -150.0 || other.List[6] != 16.0 {
		t.Fatalf("positional values = %#v", other.List)
	}
	if n := other.List[7].(*Table).Sub("nested"); len(n.List) != 3 {
		t.Fatalf("nested list = %#v", n)
	}
	for _, bad := range []string{`X = {`, `X = "open`, `X = os.exit()`, `X = { [1] 2 }`} {
		if _, err := ParseSavedVariables(bad); err == nil {
			t.Errorf("expected an error for %q", bad)
		}
	}
}

func fakeWTF(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	sv := filepath.Join(dir, "Account", "12345#1", "SavedVariables")
	if err := os.MkdirAll(sv, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(sv, "FafnirsBed.lua"), []byte(fakeSV), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestLoad(t *testing.T) {
	h, err := Load(Config{WTF: fakeWTF(t), Addon: "FafnirsBed", Var: "FafnirsBedDB"})
	if err != nil {
		t.Fatal(err)
	}
	if len(h.Characters) != 2 || h.Characters[0].Name != "Astrid" || h.Characters[0].Realm != "Test Realm" {
		t.Fatalf("characters = %+v", h.Characters)
	}
	a, b := h.Characters[0], h.Characters[1]
	if a.Copper != 12345 || a.Items != 23 || a.Distinct != 3 || !a.BankRead || !a.MailRead || len(a.Bank) != 1 {
		t.Fatalf("Astrid = %+v", a)
	}
	if b.BankRead || b.Items != 4 || len(b.Bags) != 2 {
		t.Fatalf("Bjorn (zero-count stacks dropped, bank never opened) = %+v", b)
	}
	if h.Copper != 12634 {
		t.Fatalf("copper = %d", h.Copper)
	}
	if h.Items[0].ID != 2589 || h.Items[0].Total != 20 || len(h.Items[0].Holders) != 2 || h.Items[0].Holders[0].Count != 16 {
		t.Fatalf("top item = %+v", h.Items[0])
	}
	var hearth Item
	for _, it := range h.Items {
		if it.ID == 6948 {
			hearth = it
		}
	}
	if hearth.Total != 2 || hearth.Name != "Hearthstone" {
		t.Fatalf("hearthstone across characters = %+v", hearth)
	}
	for _, it := range h.Items {
		if it.ID == 999999 && it.Name != "" {
			t.Fatalf("an unnamed item keeps an empty name for the page to label: %+v", it)
		}
	}
}

func TestLoadNeedsTheFile(t *testing.T) {
	if _, err := Load(Config{WTF: t.TempDir(), Addon: "FafnirsBed", Var: "FafnirsBedDB"}); err == nil || !strings.Contains(err.Error(), "/reload") {
		t.Fatalf("a missing file should say how to make one, got %v", err)
	}
}

func TestConfigNeedsWTF(t *testing.T) {
	t.Setenv("WOW_WTF_DIR", "")
	if _, err := configFromEnv(); err == nil {
		t.Fatal("WOW_WTF_DIR has no default")
	}
}

func TestHTTP(t *testing.T) {
	srv := httptest.NewServer(handler(Config{WTF: fakeWTF(t), Addon: "FafnirsBed", Var: "FafnirsBedDB"}))
	defer srv.Close()
	r, err := http.Get(srv.URL + "/api/hoard")
	if err != nil {
		t.Fatal(err)
	}
	var h Hoard
	if err := json.NewDecoder(r.Body).Decode(&h); err != nil || len(h.Characters) != 2 {
		t.Fatalf("api: %v %+v", err, h)
	}
	r, _ = http.Get(srv.URL + "/")
	if r.StatusCode != 200 || r.Header.Get("Cache-Control") != "no-store" {
		t.Fatalf("page: %d %q", r.StatusCode, r.Header.Get("Cache-Control"))
	}
	r, _ = http.Get(srv.URL + "/../etc/passwd")
	if r.StatusCode != 404 {
		t.Fatalf("other paths 404, got %d", r.StatusCode)
	}
}

func TestLoopbackOnly(t *testing.T) {
	for addr, want := range map[string]bool{"127.0.0.1:8765": true, "localhost:1": true, "[::1]:8765": true, "0.0.0.0:8765": false, ":8765": false, "192.168.1.5:80": false} {
		if loopback(addr) != want {
			t.Errorf("loopback(%q) != %v", addr, want)
		}
	}
}
