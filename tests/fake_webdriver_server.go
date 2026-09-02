// tests/fake_webdriver_server.go — minimal fake WebDriver server (W3C
// protocol) for testing PARENA's net/webdriver.prn end-to-end without a
// real browser/chromedriver installed.
//
// Real, honest limitation: this stands in for a real driver
// (chromedriver/geckodriver) so `make test-webdriver` has something to
// run against in a box with no browser installed -- it verifies the
// wire protocol (request shapes, response parsing, the full session
// lifecycle) is correct, not that a real browser actually
// navigates/renders/clicks anything. Point net/webdriver.prn at a real
// chromedriver's own port instead to get a genuine browser-backed run
// of the same test.
//
// Go, not Python (founder, real-time: "go is fine" / "just dont use
// python") -- also means responses go out with real Go net/http
// framing (proper \r\n line endings) rather than needing any bare-\n
// leniency on the client side.
package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"regexp"
)

const elemKey = "element-6066-11e4-a52e-4f735466cecf"

type route struct {
	method  string
	pattern *regexp.Regexp
	handle  func(w http.ResponseWriter)
}

// writeJSON uses Go's json.Encoder default (HTML-escaping ON, '<'/'>'/'&'
// encoded as \uXXXX). Verified live 2026-09-02 against a REAL chromedriver +
// Chrome for Testing session (real page-source over the wire against
// https://example.com): real drivers DO escape angle brackets this exact
// way -- an earlier version of this comment/fixture assumed the opposite
// and disabled escaping, which was backwards; see PARENA's own CHANGELOG
// for the full real correction. The actual bug that assumption was papering
// over lived in host_json_unescape's own \u handling (fixed in
// tests/test_webdriver.c), not in this fixture.
func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(v)
}

func main() {
	port := "9515"
	if len(os.Args) > 1 {
		port = os.Args[1]
	}

	routes := []route{
		{"POST", regexp.MustCompile(`^/session$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": map[string]any{"sessionId": "fake-session-123", "capabilities": map[string]any{}}})
		}},
		{"POST", regexp.MustCompile(`^/session/[^/]+/url$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": nil})
		}},
		{"POST", regexp.MustCompile(`^/session/[^/]+/element$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": map[string]string{elemKey: "fake-element-456"}})
		}},
		{"POST", regexp.MustCompile(`^/session/[^/]+/element/[^/]+/click$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": nil})
		}},
		{"POST", regexp.MustCompile(`^/session/[^/]+/element/[^/]+/value$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": nil})
		}},
		{"GET", regexp.MustCompile(`^/session/[^/]+/element/[^/]+/text$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": "Hello World"})
		}},
		{"GET", regexp.MustCompile(`^/session/[^/]+/title$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": "Test Page"})
		}},
		{"GET", regexp.MustCompile(`^/session/[^/]+/source$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": "<html><body><h1>Fake Rendered Page</h1></body></html>"})
		}},
		{"DELETE", regexp.MustCompile(`^/session/[^/]+$`), func(w http.ResponseWriter) {
			writeJSON(w, map[string]any{"value": nil})
		}},
	}

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		for _, rt := range routes {
			if r.Method == rt.method && rt.pattern.MatchString(r.URL.Path) {
				rt.handle(w)
				return
			}
		}
		w.WriteHeader(http.StatusNotFound)
		writeJSON(w, map[string]any{"value": map[string]string{"error": "unknown command", "message": r.Method + " " + r.URL.Path}})
	})

	log.Printf("fake-webdriver listening on :%s", port)
	log.Fatal(http.ListenAndServe("127.0.0.1:"+port, nil))
}
