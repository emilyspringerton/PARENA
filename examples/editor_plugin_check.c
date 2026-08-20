/* editor_plugin_check.c — real compile-check wrapper for the generated
 * examples/editor_plugin.c: includes the honest host-stub declarations
 * first (see editor_plugin_host_stubs.h's own header comment), then the
 * genrule's own output, so //examples:editor_plugin_compiled holds it to
 * the real "-std=c99 -Wall -Wextra -pedantic -Werror, zero warnings" bar
 * without needing Bazel's -include/$(execpath) machinery for a single
 * forced header (a plain #include here is simpler and doesn't depend on
 * cc_library's location-expansion rules for hdrs vs. srcs).
 */
#include "editor_plugin_host_stubs.h"
#include "editor_plugin.c"
