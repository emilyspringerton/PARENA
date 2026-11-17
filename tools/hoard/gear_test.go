package main

// Made-up data only (D-8): the founder's real SavedVariables never enter this repo.

import (
	"bytes"
	"encoding/csv"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const gearSV = `
FafnirsBedDB = {
["v"] = 1,
["names"] = {
[2589] = "Linen Cloth",
[5009] = "Mindbender Loop",
[2207] = "Jambiya",
[4373] = "Shadowskin Gloves",
},
["post"] = {
["Bjorn-Test Realm"] = {
[1] = { ["from"] = "Astrid-Test Realm", ["at"] = 1794738000, ["copper"] = 0, ["items"] = { [2589] = 5, }, },
},
["Stranger-Test Realm"] = {
[2] = { ["from"] = "Astrid-Test Realm", ["at"] = 1794738100, ["copper"] = 0, ["items"] = { [2589] = 1, }, },
},
},
["chars"] = {
["Astrid-Test Realm"] = {
["seen"] = 1794738458,
["slots"] = {
[16] = 2207,
[10] = 4373,
[11] = 5009,
[12] = 5009,
},
["loc"] = {
["equipped"] = {
[2207] = 1,
[4373] = 1,
[5009] = 2,
},
["bags"] = {
[2589] = 16,
},
},
["gold"] = 100,
},
["Bjorn-Test Realm"] = {
["seen"] = 1794700000,
["loc"] = {
["bags"] = {
[5009] = 1,
},
},
["gold"] = 0,
},
},
}
`

func gearWTF(t *testing.T) Config {
	t.Helper()
	dir := t.TempDir()
	sv := filepath.Join(dir, "Account", "12345#1", "SavedVariables")
	if err := os.MkdirAll(sv, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(sv, "FafnirsBed.lua"), []byte(gearSV), 0o644); err != nil {
		t.Fatal(err)
	}
	return Config{WTF: dir, Addon: "FafnirsBed", Var: "FafnirsBedDB"}
}

func TestGear(t *testing.T) {
	h, err := Load(gearWTF(t))
	if err != nil {
		t.Fatal(err)
	}
	a, b := h.Characters[0], h.Characters[1]
	if !a.GearRead || len(a.Gear) != 4 || a.Gear[0].SlotName != "Hands" || a.Gear[3].SlotName != "Main hand" || a.Gear[3].Name != "Jambiya" {
		t.Fatalf("Astrid's gear, in slot order = %+v", a.Gear)
	}
	if a.Items != 20 || a.Distinct != 4 {
		t.Fatalf("worn gear counts as held: items %d distinct %d", a.Items, a.Distinct)
	}
	if b.Bank == nil || b.Mail == nil || b.Gear == nil || b.Equipped == nil {
		t.Fatalf("unread locations must be [] for the page, not null: %+v", b)
	}
	if b.GearRead || len(b.Gear) != 0 {
		t.Fatalf("Bjorn was not seen since gear reading: unknown, not empty = %+v", b)
	}
	for _, it := range h.Items {
		if it.ID == 5009 && (it.Total != 3 || it.Holders[0].Where != "equipped" || it.Holders[0].Count != 2) {
			t.Fatalf("the ring: 2 worn by Astrid + 1 in Bjorn's bags = %+v", it)
		}
	}
}

func TestCSV(t *testing.T) {
	h, err := Load(gearWTF(t))
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	if err := WriteCSV(&buf, h); err != nil {
		t.Fatal(err)
	}
	recs, err := csv.NewReader(&buf).ReadAll()
	if err != nil {
		t.Fatal(err)
	}
	want := [][]string{
		{"character", "realm", "account", "location", "slot", "item_id", "name", "count"},
		{"Astrid", "Test Realm", "12345#1", "equipped", "Hands", "4373", "Shadowskin Gloves", "1"},
		{"Astrid", "Test Realm", "12345#1", "equipped", "Finger 1", "5009", "Mindbender Loop", "1"},
		{"Astrid", "Test Realm", "12345#1", "equipped", "Finger 2", "5009", "Mindbender Loop", "1"},
		{"Astrid", "Test Realm", "12345#1", "equipped", "Main hand", "2207", "Jambiya", "1"},
		{"Astrid", "Test Realm", "12345#1", "bags", "", "2589", "Linen Cloth", "16"},
		{"Bjorn", "Test Realm", "12345#1", "bags", "", "5009", "Mindbender Loop", "1"},
		{"Bjorn", "Test Realm", "12345#1", "post", "", "2589", "Linen Cloth", "5"},
	}
	if len(recs) != len(want) {
		t.Fatalf("rows (mail to Stranger has left the account) = %q", recs)
	}
	for i := range want {
		if strings.Join(recs[i], "|") != strings.Join(want[i], "|") {
			t.Errorf("row %d = %q, want %q", i, recs[i], want[i])
		}
	}
}

func TestCSVOverHTTP(t *testing.T) {
	srv := httptest.NewServer(handler(gearWTF(t)))
	defer srv.Close()
	r, err := http.Get(srv.URL + "/api/hoard.csv")
	if err != nil {
		t.Fatal(err)
	}
	body, _ := io.ReadAll(r.Body)
	if r.StatusCode != 200 || !strings.HasPrefix(r.Header.Get("Content-Type"), "text/csv") ||
		!strings.HasPrefix(string(body), "character,realm,account,location,slot,item_id,name,count\n") {
		t.Fatalf("csv endpoint: %d %q %q", r.StatusCode, r.Header.Get("Content-Type"), body)
	}
}
