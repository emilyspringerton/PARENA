# IDUNA discovery: a local IDUNA first, the cloud one otherwise

Founder, 2026-11-17: *"we need a local iduna to test against it needs to work so that it checks to see if there is a
local iduna and connects to that and if not it attempts to use cloud iduna"*.

Every IDUNA client asks the same question: which IDUNA do I talk to? PARENA answers it once, and C tools, Lua tools and
the WOTAN pages all run that same answer.

## The pieces

| file | target | what it does |
| --- | --- | --- |
| `stdlib/iduna/discover.prn` | C, Lua, JS, TS | **The rules:** probe order, verdicts, per-candidate timeouts, session cache. Plain `I32` functions. |
| `stdlib/iduna/discover_parity.txt` | all | The shared fixture. The C, Lua and JS suites all assert it. |
| `stdlib/net/http_probe.prn` + `runtime/parena_runtime.h` ("net/http_probe.prn host glue") | C | **A bounded GET.** The connect and every read share one deadline. On POSIX and Windows, that deadline is split across a name's addresses. |
| `stdlib/iduna/discover_native.prn` | C | **One native probe.** `GET <base>/health`, judged by the rules. |
| `tools/iduna_discover_host.c` | C | **The `iduna-discover` CLI host:** environment, printing, exit code. |
| `stdlib/iduna/discover_web.prn` | JS | **The browser prober.** `fetch` with an abort timer, one candidate at a time. |

## The rules

A probe is one `GET <base>/health`. IDUNA answers `{"ok":true,"service":"iduna",...}`. Anything else, including some
other server's 200, is **not IDUNA**. BOOTSTRAP.md records a box where `localhost:8080` was a different server.

Candidates are probed in this order:

1. **The page's own origin** (browser only). It is IDUNA when IDUNA serves the page itself (`WOTAN_SITE_DIR`), or when
   nginx sends `/api/` to IDUNA. So it is asked at `/api/v1/health` first, then `/health`.
2. **The local IDUNA**, `IDUNA_LOCAL_URL`. Default `http://localhost:8080`, IDUNA's own `IDUNA_ADDR` / `BASE_URL`
   defaults.
3. **The cloud IDUNA**, `IDUNA_CLOUD_URL`. Default `https://iam.okemily.com`, from EMILY
   `gitops/clusters/prrject-fatbaby/92-iduna-routes.yaml`.

The first candidate that answers as IDUNA wins. If none does, the client **attempts the cloud anyway** and reports what
each candidate did.

Some details:
- **Who probes the local IDUNA.** Native tools, `localhost` pages and `file://` pages probe it by default. A page served
  from anywhere else does not: a browser would ask that visitor for local-network access, and a stranger's localhost is
  not this deployment's IDUNA. A developer can force it either way with `IDUNA_LOCAL_FIRST=1|0` (native) or
  localStorage `iduna_local_first` (browser).
- **Timeouts.** Local 1000 ms, cloud 4000 ms, and own origin 5000 ms: that origin just served the page, so slow means
  busy. `IDUNA_PROBE_TIMEOUT_MS` overrides all three, clamped to 100-30000 ms.
- **Cache.** A verified choice is reused for 10 minutes, except a local one, at most 60 s, because a developer starts and
  stops it. An unverified one is re-checked after 15 s, so an IDUNA started a moment later is found on the next page.

## Native: `iduna-discover`

```bash
bash scripts/build_iduna_discover.sh build/iduna-discover
export IDUNA_BASE_URL="$(build/iduna-discover)"
```

- **stdout:** the base URL.
- **stderr:** one line per candidate.
- **Exit codes:**
  - `0` means an IDUNA answered.
  - `2` means none answered, and the printed base is the cloud, attempted but not confirmed.
  - `1` means a URL in the environment can't be used.
- **Limit:** an `https` cloud is not checked natively, because PARENA's C runtime has no TLS yet. It counts as no
  answer, and the line says so.

## Browser: `/js/iduna-discover.js`

```bash
bash scripts/build_iduna_discover_js.sh ../WOTAN/js/iduna-discover.js   # PARENA straight to JS, no Node
```

`idunaDiscover(opts)` resolves to `{source, base, loginUrl, verified, error, checks}`. `base` is `""` for the page's own
origin. `idunaForget()` makes the next call probe again. WOTAN's `js/iduna-sso.js` is the host:
- It keeps the choice in sessionStorage.
- It reads `window.IDUNA_CONFIG` and the developer's localStorage overrides.
- It exposes `idunaReady()`, `idunaApi(path, init)` and `idunaChoiceNow()`.

A page on another origin can only read an IDUNA that allows that origin: IDUNA's `IDUNA_CORS_ORIGINS`, which is off
unless set. A page served by IDUNA itself (`WOTAN_SITE_DIR`) needs nothing.

## Tests

```bash
LUA51=lua5.1 bash scripts/test_iduna_discover.sh build/iduna-discover-test
```

| suite | what it checks |
| --- | --- |
| C | the fixture, then the probe against a scripted server on 127.0.0.1 |
| Lua | the fixture under lua5.1 |
| JS | writes `discover_test.html`, which asserts the fixture in a browser (there is no Node on the dev box) |

The C probe cases are:
- IDUNA's health answer;
- an HTML 404, and someone else's 200;
- an IDUNA that says it is not ok;
- silence, and stalled headers;
- an oversized body and a non-HTTP banner;
- a closed port;
- `localhost` when only 127.0.0.1 listens.
