/* tools/ci_status.h -- the real forward declaration for
 * ci_status_check_c(), the host-side implementation
 * tools/ci_status_host.c actually provides. Included into the generated
 * C (via `gcc -include`, not a modification to the generated file
 * itself) so it's visible where stdlib/ci/status.prn's own #target
 * body calls it, and into ci_status_host.c's own real definition, so
 * both translation units agree on the one real signature.
 */
#ifndef CI_STATUS_H
#define CI_STATUS_H

int ci_status_check_c(const char *repo, const char *sha, const char *token);

#endif /* CI_STATUS_H */
