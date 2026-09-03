"""parena_compile.bzl — a real, reusable Bazel macro for a consuming repo's own co-located
`.prn` source (kanban priority-queue cards PX-001/PX-BZ-001: "we need to add support to commit
the .prn file right in the same directory as the generated output... not sure how that would
work with bazel and other build tools").

Real, checked-not-assumed finding before writing this: `parena build <file>.prn -o <out>` already
accepts a `.prn` file at ANY path today — confirmed live, there is no compiler-side restriction
requiring `.prn` source to live inside this repo's own `stdlib/` directory. That idiom
(`STDLIB.md`'s own documented convention) is a real, social/organizational choice, not an
enforced one. **Two real, already-working, hand-rolled precedents already exist in this same
monorepo** before this macro: `DUNG/parena/entry.prn` (co-located `.prn` source, a `//go:generate`
directive regenerating its own checked-in `internal/burrowgen/entry_gen.go`, wired into a plain
`go_library` Bazel rule listing the pre-generated file) and `ladybug/BUILD.bazel` (a real genrule
invoking `@parena//src:parena` — this exact real Bazel target — directly against a co-located
`.prn` file). This macro is the real, missing piece those two examples didn't have yet: a
SHARED, reusable rule so a THIRD (and every future) adopting repo doesn't need to hand-write the
same genrule boilerplate `ladybug/BUILD.bazel` did.

Real, deliberate v0 scope: the C target only (`parena build ... -o *.c`), matching this whole
repo's own "C first" precedent (VS0's own original, most mature target). A real, separate,
analogous macro for BURROW's own Go/TS/Java targets is real, honest, later work — not attempted
here, since it would live in the BURROW repo (against `@burrow//:burrow`, a different Bazel
module) rather than this one.
"""

def parena_compile_c(name, prn, out, deps = []):
    """Compiles one `.prn` file to C via the real `parena` binary, as a Bazel genrule.

    Args:
        name: the real Bazel target name for this genrule.
        prn: the real `.prn` source file (a `label`, e.g. `"cli_mod.prn"`) — this is the file a
            consuming repo commits right alongside its own BUILD file, the real ask this macro
            answers.
        out: the real output `.c` file path this genrule produces (e.g. `"cli_mod_gen.c"`).
        deps: real, additional `.prn` files needed in the SAME `parena build` invocation (e.g.
            `stdlib/string.prn`) — real, necessary for the same reason `STDLIB.md`'s own
            documented multi-file build trap exists: a called-but-not-combined function's return
            type silently falls back to a wrong guess otherwise. Passed as extra `srcs`.
    """
    all_srcs = deps + [prn]
    src_locations = " ".join(["$(location %s)" % s for s in all_srcs])
    native.genrule(
        name = name,
        srcs = all_srcs,
        outs = [out],
        cmd = "$(location @parena//src:parena) build " + src_locations + " -o $@",
        tools = ["@parena//src:parena"],
    )
