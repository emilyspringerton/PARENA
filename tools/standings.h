/* tools/standings.h -- forward declaration for standings_print_c(), the
 * host-side implementation stdlib/league/standings.prn's own `#target`
 * body calls (same split as tools/ci_status.h). */
#ifndef STANDINGS_H
#define STANDINGS_H

int standings_print_c(const char *base_url, const char *game, int top);

#endif /* STANDINGS_H */
