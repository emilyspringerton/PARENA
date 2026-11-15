package main

// A reader for WoW SavedVariables files: `Name = <lua value>` assignments whose values are table constructors,
// strings, numbers and booleans. It is not a Lua interpreter and runs nothing; anything else is an error.

import (
	"fmt"
	"strconv"
	"strings"
)

// Table is a Lua table: every key as its Lua literal text ("v", "4540"), plus the positional values in order.
type Table struct {
	Keys  []string
	Field map[string]any
	List  []any
}

func (t *Table) Get(k string) any {
	if t == nil {
		return nil
	}
	return t.Field[k]
}

func (t *Table) Sub(k string) *Table {
	s, _ := t.Get(k).(*Table)
	return s
}

func (t *Table) Num(k string) float64 {
	n, _ := t.Get(k).(float64)
	return n
}

type svParser struct {
	src string
	pos int
}

// ParseSavedVariables returns every top-level assignment in the file.
func ParseSavedVariables(src string) (map[string]any, error) {
	p := &svParser{src: src}
	out := map[string]any{}
	for {
		p.skip()
		if p.pos >= len(p.src) {
			return out, nil
		}
		name := p.ident()
		if name == "" {
			return nil, p.errf("expected a variable name")
		}
		p.skip()
		if !p.eat('=') {
			return nil, p.errf("expected '=' after %s", name)
		}
		v, err := p.value()
		if err != nil {
			return nil, err
		}
		out[name] = v
	}
}

func (p *svParser) errf(format string, a ...any) error {
	line := 1 + strings.Count(p.src[:p.pos], "\n")
	return fmt.Errorf("SavedVariables line %d: %s", line, fmt.Sprintf(format, a...))
}

func (p *svParser) skip() {
	for p.pos < len(p.src) {
		c := p.src[p.pos]
		switch {
		case c == ' ' || c == '\t' || c == '\r' || c == '\n':
			p.pos++
		case strings.HasPrefix(p.src[p.pos:], "--[["):
			end := strings.Index(p.src[p.pos:], "]]")
			if end < 0 {
				p.pos = len(p.src)
			} else {
				p.pos += end + 2
			}
		case strings.HasPrefix(p.src[p.pos:], "--"):
			end := strings.IndexByte(p.src[p.pos:], '\n')
			if end < 0 {
				p.pos = len(p.src)
			} else {
				p.pos += end + 1
			}
		default:
			return
		}
	}
}

func (p *svParser) eat(c byte) bool {
	if p.pos < len(p.src) && p.src[p.pos] == c {
		p.pos++
		return true
	}
	return false
}

func isIdent(c byte, first bool) bool {
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (!first && c >= '0' && c <= '9')
}

func (p *svParser) ident() string {
	start := p.pos
	for p.pos < len(p.src) && isIdent(p.src[p.pos], p.pos == start) {
		p.pos++
	}
	return p.src[start:p.pos]
}

func (p *svParser) value() (any, error) {
	p.skip()
	if p.pos >= len(p.src) {
		return nil, p.errf("unexpected end of file")
	}
	c := p.src[p.pos]
	switch {
	case c == '{':
		return p.table()
	case c == '"' || c == '\'':
		return p.str()
	case c == '-' || c == '.' || (c >= '0' && c <= '9'):
		return p.number()
	}
	switch w := p.ident(); w {
	case "true":
		return true, nil
	case "false":
		return false, nil
	case "nil":
		return nil, nil
	default:
		return nil, p.errf("unexpected %q", w)
	}
}

func (p *svParser) number() (float64, error) {
	start := p.pos
	if p.src[p.pos] == '-' {
		p.pos++
	}
	if strings.HasPrefix(p.src[p.pos:], "0x") || strings.HasPrefix(p.src[p.pos:], "0X") {
		p.pos += 2
		for p.pos < len(p.src) && strings.IndexByte("0123456789abcdefABCDEF", p.src[p.pos]) >= 0 {
			p.pos++
		}
		n, err := strconv.ParseInt(strings.Replace(strings.ToLower(p.src[start:p.pos]), "0x", "", 1), 16, 64)
		if err != nil {
			return 0, p.errf("bad number %q", p.src[start:p.pos])
		}
		return float64(n), nil
	}
	for p.pos < len(p.src) && strings.IndexByte("0123456789.eE+-", p.src[p.pos]) >= 0 {
		if (p.src[p.pos] == '+' || p.src[p.pos] == '-') && p.pos > start && p.src[p.pos-1] != 'e' && p.src[p.pos-1] != 'E' {
			break
		}
		p.pos++
	}
	text := p.src[start:p.pos]
	n, err := strconv.ParseFloat(text, 64)
	if err != nil {
		return 0, p.errf("bad number %q", text)
	}
	return n, nil
}

func (p *svParser) str() (string, error) {
	q := p.src[p.pos]
	p.pos++
	var b strings.Builder
	for p.pos < len(p.src) {
		c := p.src[p.pos]
		switch {
		case c == q:
			p.pos++
			return b.String(), nil
		case c == '\n':
			return "", p.errf("newline in string")
		case c == '\\':
			p.pos++
			if p.pos >= len(p.src) {
				return "", p.errf("unfinished escape")
			}
			e := p.src[p.pos]
			switch e {
			case 'n':
				b.WriteByte('\n')
			case 't':
				b.WriteByte('\t')
			case 'r':
				b.WriteByte('\r')
			case 'a':
				b.WriteByte('\a')
			case 'b':
				b.WriteByte('\b')
			case 'f':
				b.WriteByte('\f')
			case 'v':
				b.WriteByte('\v')
			case '\n':
				b.WriteByte('\n')
			case '\\', '"', '\'':
				b.WriteByte(e)
			default:
				if e >= '0' && e <= '9' {
					start := p.pos
					for p.pos < len(p.src) && p.pos-start < 3 && p.src[p.pos] >= '0' && p.src[p.pos] <= '9' {
						p.pos++
					}
					n, _ := strconv.Atoi(p.src[start:p.pos])
					if n > 255 {
						return "", p.errf("escape \\%d is out of range", n)
					}
					b.WriteByte(byte(n))
					continue
				}
				return "", p.errf("unknown escape \\%c", e)
			}
			p.pos++
		default:
			b.WriteByte(c)
			p.pos++
		}
	}
	return "", p.errf("unfinished string")
}

func (p *svParser) table() (*Table, error) {
	p.pos++ // {
	t := &Table{Field: map[string]any{}}
	for {
		p.skip()
		if p.eat('}') {
			return t, nil
		}
		var key string
		var hasKey bool
		if p.pos < len(p.src) && p.src[p.pos] == '[' {
			p.pos++
			k, err := p.value()
			if err != nil {
				return nil, err
			}
			p.skip()
			if !p.eat(']') {
				return nil, p.errf("expected ']'")
			}
			p.skip()
			if !p.eat('=') {
				return nil, p.errf("expected '=' after a [key]")
			}
			key, hasKey = keyText(k), true
		} else if save := p.pos; p.pos < len(p.src) && isIdent(p.src[p.pos], true) {
			name := p.ident()
			p.skip()
			if p.eat('=') {
				key, hasKey = name, true
			} else {
				p.pos = save
			}
		}
		v, err := p.value()
		if err != nil {
			return nil, err
		}
		if hasKey {
			if _, dup := t.Field[key]; !dup {
				t.Keys = append(t.Keys, key)
			}
			t.Field[key] = v
		} else {
			t.List = append(t.List, v)
		}
		p.skip()
		if !p.eat(',') && !p.eat(';') {
			p.skip()
			if !p.eat('}') {
				return nil, p.errf("expected ',' or '}'")
			}
			return t, nil
		}
	}
}

func keyText(k any) string {
	switch v := k.(type) {
	case float64:
		return strconv.FormatFloat(v, 'f', -1, 64)
	case string:
		return v
	case bool:
		return strconv.FormatBool(v)
	default:
		return fmt.Sprint(v)
	}
}
