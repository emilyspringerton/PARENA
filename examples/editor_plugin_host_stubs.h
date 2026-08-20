/* editor_plugin_host_stubs.h — real, honest extern declarations for the
 * host-side symbols examples/editor_plugin.prn's own #target/inline-c
 * bodies call (editor_register_command, editor_get_config_option). Not
 * an implementation -- the real dispatch table these belong to is
 * genuinely unstarted work (see editor_plugin.prn's own header comment,
 * NORTHSTAR.md's "editor shell still an open, undecided question").
 * This exists purely so //examples:editor_plugin_compiled can hold the
 * generated C to the same real "compiles with zero warnings under
 * -std=c99 -Wall -Wextra -pedantic -Werror" bar every other VS0 domain 3
 * example already meets -- without it, gcc correctly flags calling an
 * undeclared function as -Werror=implicit-function-declaration, which
 * would silently mask a real new warning class showing up in this file's
 * own future changes.
 *
 * Corrected 2026-08-20: `name`/`key` are declared `String @ :region/
 * scratch` in editor_plugin.prn's own real source -- they used to emit
 * as `Arena *`, a real bug in the compiler's own has_region_marker()
 * (fixed the same day: that function's own doc always said "Arena @
 * ...", but the implementation matched ANY `@`/keyword regardless of
 * the type token before it). Now that the compiler emits the real,
 * correct `char *`, this stub header is updated to match -- it was
 * only ever "correct" against the bug, not against the real intended
 * type.
 */
#ifndef EDITOR_PLUGIN_HOST_STUBS_H
#define EDITOR_PLUGIN_HOST_STUBS_H

#include "parena_runtime.h"

void editor_register_command(char *name, void (*handler)(void));
Option editor_get_config_option(char *key);

#endif /* EDITOR_PLUGIN_HOST_STUBS_H */
