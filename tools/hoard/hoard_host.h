/* tools/hoard/hoard_host.h -- prototypes for the thin C host behind tools/hoard/hoard.prn.
 *
 * The generated C emits each `#target` body verbatim, so the bodies need these declarations visible first: build
 * with `-include hoard_host.h` (the same precedent as SHANKPIT's tools/local_nock_host.h and PARENA's
 * tools/ci_status.h).
 *
 * Only what PARENA cannot do itself yet: arenas, env, reading a file, listing the SavedVariables files (the one
 * place that branches by platform), and quoting a command line for the platform's popen. Routing, configuration,
 * the ledger command and every response are PARENA in hoard.prn. Strings are `char *` (a PARENA String), never
 * const-qualified, so the emitted bodies compile under -Werror.
 */
#ifndef HOARD_HOST_H
#define HOARD_HOST_H

#include "parena_runtime.h"

Arena *hd_state_arena(void);
Arena *hd_request_arena(void);
int hd_request_reset(void);

char *hd_env(const char *key, const char *fallback, Arena *dest);
char *hd_read_file(const char *path, Arena *dest);

char *hd_sv_list(const char *wtf, const char *addon, Arena *dest);
int hd_sv_saved_at(const char *wtf, const char *addon);

char *hd_shell_wrap(const char *cmd, Arena *dest);

/* hoard_run -- the PARENA entry point main() calls. */
int hoard_run(Arena *dest);

#endif
