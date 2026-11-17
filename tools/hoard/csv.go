package main

// The hoard as one flat table: a row per item stack per character per location, and a row per equipped slot.
// Columns: character, realm, account, location, slot, item_id, name, count.
//
//	location  equipped | bags | bank | mail | post (mail in flight to one of your own characters)
//	slot      the equipment slot ("Head", "Main hand", ...) for equipped rows; empty elsewhere, because
//	          Fafnir's Bed keeps bags, bank and mail as counts per item, not per bag slot
//	name      the item's name as the client last showed it; empty when it was never seen in a link
//
// Mail sent to someone outside the account is not a row: it has left the hoard.

import (
	"encoding/csv"
	"io"
	"sort"
	"strconv"
)

var csvHeader = []string{"character", "realm", "account", "location", "slot", "item_id", "name", "count"}

var locationOrder = map[string]int{"equipped": 0, "bags": 1, "bank": 2, "mail": 3, "post": 4}

type csvRow struct {
	char, realm, account, loc string
	slot                      int // -1 when not an equipped slot
	id                        int
	name                      string
	count                     int
}

// Rows flattens the hoard, sorted by character, location, slot, then item name and id.
func Rows(h *Hoard) [][]string {
	var rows []csvRow
	accountOf := map[string]string{}
	for _, ch := range h.Characters {
		accountOf[ch.Key] = ch.Account
		add := func(loc string, list []Stack) {
			for _, s := range list {
				rows = append(rows, csvRow{ch.Name, ch.Realm, ch.Account, loc, -1, s.ID, s.Name, s.Count})
			}
		}
		if ch.GearRead {
			for _, g := range ch.Gear {
				rows = append(rows, csvRow{ch.Name, ch.Realm, ch.Account, "equipped", g.Slot, g.ID, g.Name, 1})
			}
		} else {
			add("equipped", ch.Equipped)
		}
		add("bags", ch.Bags)
		add("bank", ch.Bank)
		add("mail", ch.Mail)
	}
	for _, p := range h.Post {
		if !p.OnAccount {
			continue
		}
		realm := ""
		if len(p.To) > len(p.ToName)+1 {
			realm = p.To[len(p.ToName)+1:]
		}
		for _, s := range p.Items {
			rows = append(rows, csvRow{p.ToName, realm, accountOf[p.To], "post", -1, s.ID, s.Name, s.Count})
		}
	}
	sort.SliceStable(rows, func(a, b int) bool {
		x, y := rows[a], rows[b]
		if x.char != y.char {
			return x.char < y.char
		}
		if x.realm != y.realm {
			return x.realm < y.realm
		}
		if locationOrder[x.loc] != locationOrder[y.loc] {
			return locationOrder[x.loc] < locationOrder[y.loc]
		}
		if x.slot != y.slot {
			return x.slot < y.slot
		}
		if x.name != y.name {
			return x.name < y.name
		}
		return x.id < y.id
	})
	out := make([][]string, 0, len(rows))
	for _, r := range rows {
		slot := ""
		if r.slot >= 0 {
			slot = slotName(r.slot)
		}
		out = append(out, []string{r.char, r.realm, r.account, r.loc, slot, strconv.Itoa(r.id), r.name, strconv.Itoa(r.count)})
	}
	return out
}

// WriteCSV writes the header and every row.
func WriteCSV(w io.Writer, h *Hoard) error {
	cw := csv.NewWriter(w)
	if err := cw.Write(csvHeader); err != nil {
		return err
	}
	if err := cw.WriteAll(Rows(h)); err != nil {
		return err
	}
	cw.Flush()
	return cw.Error()
}
