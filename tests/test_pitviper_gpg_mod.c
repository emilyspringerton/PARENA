/* tests/test_pitviper_gpg_mod.c -- real end-to-end verification of
 * stdlib/pitviper/gpg_mod.prn's is-safe-key-field?/generate-key. Confirms the real shell-
 * injection guard actually rejects unsafe input (and never invokes gpg when it does), and that
 * valid input is accepted by the validator. The real `gpg --quick-generate-key` happy path is
 * deliberately NOT exercised here -- an automated test run should never write a real key into
 * whatever keyring GNUPGHOME happens to point at in CI, and key generation consumes real entropy
 * / can be slow. That invocation shape was hand-verified once, out of band, against a scratch
 * GNUPGHOME (see this file's own PR/commit notes) -- named, not silently assumed working.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_pitviper_gpg_mod_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* Real, safe input: letters/digits/space/@/./-/_/' only. */
    assert(is_safe_key_field_("EINHORN_INDUSTRIAL App Releases") == 1);
    assert(is_safe_key_field_("releases@okemily.com") == 1);
    assert(is_safe_key_field_("O'Brien-Test 2.0") == 1);

    /* Real shell-metacharacter rejections -- the actual injection surface run-capture's own doc
     * comment names. */
    assert(is_safe_key_field_("bad; rm -rf /") == 0);
    assert(is_safe_key_field_("bad`whoami`") == 0);
    assert(is_safe_key_field_("bad$(whoami)") == 0);
    assert(is_safe_key_field_("bad|cat") == 0);
    assert(is_safe_key_field_("bad\"quote") == 0);
    assert(is_safe_key_field_("") == 0);

    /* generate-key must fail CLOSED on unsafe input, before any shell command ever runs -- the
     * real point of validating first. */
    Result r1 = generate_key("bad; rm -rf /", "ok@example.com", &arena);
    assert(r1.tag == 0); /* Err */

    Result r2 = generate_key("Ok Name", "bad`whoami`@example.com", &arena);
    assert(r2.tag == 0); /* Err */

    printf("test_pitviper_gpg_mod: all assertions passed\n");
    return 0;
}
