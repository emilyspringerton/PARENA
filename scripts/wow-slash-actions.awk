# scripts/wow-slash-actions.awk -- read an addon's documented slash commands into Lua table rows for the
# Collection hub's control panel. Used by scripts/wow-package-collection.sh; POSIX awk (mawk, gawk, busybox).
#
#   awk -v cmds="/derby /fish" -f scripts/wow-slash-actions.awk addon.lua ...
#
# The convention it reads (the header comment of every addon's host glue):
#
#   -- /derby start [minutes]   a timed derby for the whole group (default 15)
#   -- /derby stop | record     end it now / the all-time biggest catch
#   -- /huginn               show or hide the panel           /huginn log      show or hide the mini quest log
#
# A line is `--`, one to three spaces, then one of the addon's own commands (`cmds`; anything else, and anything
# indented deeper is skipped, except that plain text right after a command line continues its description). Usage and description are separated by two or more
# spaces; a second command on the same line starts after three or more. In the usage, `a | b` lists alternatives
# (each its own action; a description with as many ` / `-separated parts is shared out among them), words before
# the first `<...>` or `[...]` are the subcommand, and the rest are its arguments. One row per command, subcommand
# and whether it needs an argument; the first one wins. Output: one `{ cmd = ..., sub = ..., args = ..., desc = ... },`
# per action.

function trim(s) { sub(/^ +/, "", s); sub(/ +$/, "", s); return s }
function lua(s) { gsub(/\\/, "\\\\", s); gsub(/"/, "\\\"", s); return "\"" s "\"" }

function emit(cmd, sub_, args, desc,   k) {
    k = cmd " " sub_ (args ~ /^</ ? " <>" : "")       # `/huginn` and `/huginn <n>` are two different actions
    if (k in seen) return
    seen[k] = 1
    n_rows++
    r_cmd[n_rows] = cmd; r_sub[n_rows] = sub_; r_args[n_rows] = args; r_desc[n_rows] = desc
    if (!seg_first) seg_first = n_rows
}

# One "usage  description" segment that starts with `cmd`.
function segment(cmd, text,   usage, desc, p, alts, n, parts, np, i, words, nw, j, sub_, args, d, lit) {
    p = match(text, /  +/)
    if (p) { usage = substr(text, 1, p - 1); desc = trim(substr(text, p + RLENGTH)) }
    else { usage = text; desc = "" }
    usage = trim(substr(usage, length(cmd) + 1))
    n = split(usage, alts, / \| /)
    if (n == 0) { n = 1; alts[1] = "" }
    np = split(desc, parts, / \/ /)
    for (i = 1; i <= n; i++) {
        d = (n > 1 && np == n) ? trim(parts[i]) : desc
        nw = split(trim(alts[i]), words, / +/)
        sub_ = ""; args = ""; lit = 1
        for (j = 1; j <= nw; j++) {
            if (lit && words[j] !~ /^[<\[]/) sub_ = sub_ (sub_ == "" ? "" : " ") words[j]
            else { lit = 0; args = args (args == "" ? "" : " ") words[j] }
        }
        emit(cmd, sub_, args, d)
    }
}

BEGIN { nc = split(cmds, known, / +/); for (i = 1; i <= nc; i++) mine[known[i]] = 1 }

# A deeper-indented line of plain text right after a command line continues that line's (last) description.
/^--    +[^ \/]/ && last_first {
    more = $0; sub(/^-- +/, "", more)
    for (i = last_first; i <= n_rows; i++) r_desc[i] = trim(r_desc[i] " " more)
    next
}

{ last_first = 0 }

/^--( |  |   )\// {   # no {m,n}: not every awk has interval expressions
    line = $0
    sub(/^-- +/, "", line)
    if (length($0) - length(line) > 5) next          # more than three spaces of indent: an example or a continuation
    while (line != "") {
        cmd = line; sub(/ .*/, "", cmd)
        if (!(cmd in mine)) next
        rest = substr(line, length(cmd) + 1)
        # where the next command on the same line begins: three or more spaces, then one of ours
        cut = 0; probe = rest; off = 0
        while ((p = match(probe, /   +\/[^ ]+/)) > 0) {
            c = substr(probe, p, RLENGTH); sub(/^ +/, "", c)
            if (c in mine) { cut = off + p; break }
            off = off + p + RLENGTH - 1; probe = substr(probe, p + RLENGTH)
        }
        seg_first = 0
        if (cut) { segment(cmd, cmd substr(rest, 1, cut - 1)); line = trim(substr(rest, cut)) }
        else { segment(cmd, cmd rest); line = "" }
        last_first = seg_first
    }
}

END {
    for (i = 1; i <= n_rows; i++)
        printf "      { cmd = %s, sub = %s, args = %s, desc = %s },\n", lua(r_cmd[i]), lua(r_sub[i]), lua(r_args[i]), lua(r_desc[i])
}
