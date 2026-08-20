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
 */
#ifndef EDITOR_PLUGIN_HOST_STUBS_H
#define EDITOR_PLUGIN_HOST_STUBS_H

#include "parena_runtime.h"

void editor_register_command(Arena *name, void (*handler)(void));
Option editor_get_config_option(Arena *key);

#endif /* EDITOR_PLUGIN_HOST_STUBS_H */
