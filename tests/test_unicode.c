/* tests/test_unicode.c -- stdlib/unicode.prn (+ generated unicode/ucd.prn): strict UTF-8 codec,
 * codepoint counting/offsets, properties, simple case mapping, display width. Every expected
 * number is hand-derived from the Unicode/RFC 3629 definitions, not read back from the code. */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "test_unicode_gen.c"

static void eq(const char *got, const char *want) {
    if (strcmp(got, want) != 0) { fprintf(stderr, "got [%s] want [%s]\n", got, want); assert(0); }
}

int main(void) {
    Arena a; arena_init(&a);

    /* decode: 1/2/3/4-byte forms: 'A', e-acute U+E9, euro U+20AC, U+1F600 */
    assert(utf8_decode_at("A", 0) == 0x41);
    assert(utf8_decode_at("\xc3\xa9", 0) == 0xE9);
    assert(utf8_decode_at("\xe2\x82\xac", 0) == 0x20AC);
    assert(utf8_decode_at("\xf0\x9f\x98\x80", 0) == 0x1F600);
    assert(utf8_decode_at("\xf4\x8f\xbf\xbf", 0) == 0x10FFFF);
    /* strictness: overlong NUL/slash/euro, surrogate, > U+10FFFF, lone/truncated, bad lead */
    assert(utf8_decode_at("\xc0\x80", 0) == -1);
    assert(utf8_decode_at("\xc1\xbf", 0) == -1);
    assert(utf8_decode_at("\xe0\x80\xaf", 0) == -1);
    assert(utf8_decode_at("\xf0\x80\x80\xaf", 0) == -1);
    assert(utf8_decode_at("\xed\xa0\x80", 0) == -1);      /* U+D800 */
    assert(utf8_decode_at("\xf4\x90\x80\x80", 0) == -1);  /* U+110000 */
    assert(utf8_decode_at("\xf5\x80\x80\x80", 0) == -1);
    assert(utf8_decode_at("\x80", 0) == -1);
    assert(utf8_decode_at("\xe2\x82", 0) == -1);          /* truncated at NUL */
    assert(utf8_decode_at("\xff", 0) == -1);

    assert(utf8_valid_("caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80") == 1);
    assert(utf8_valid_("ab\xff") == 0);
    assert(utf8_valid_("") == 1);

    /* count/offset: c a f e' sp ri ben sp emoji = 9 codepoints, 3+2+1+6+1+4 = 17 bytes */
    const char *s = "caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80";
    assert(utf8_count(s) == 9);
    assert(strlen(s) == 3 + 2 + 1 + 6 + 1 + 4);
    assert(utf8_offset(s, 0) == 0);
    assert(utf8_offset(s, 3) == 3);
    assert(utf8_offset(s, 4) == 5);   /* after the 2-byte e-acute */
    assert(utf8_offset(s, 5) == 6);   /* first CJK */
    assert(utf8_offset(s, 6) == 9);   /* second CJK */
    assert(utf8_offset(s, 7) == 12);  /* the space after it */
    assert(utf8_offset(s, 8) == 13);  /* emoji */
    assert(utf8_offset(s, 9) == (int)strlen(s));
    assert(utf8_offset(s, 10) == -1);
    assert(utf8_offset(s, -1) == -1);
    assert(utf8_prev(s, 5) == 3);
    assert(utf8_prev(s, (int)strlen(s)) == (int)strlen(s) - 4);
    assert(utf8_prev(s, 0) == 0);
    assert(utf8_count("a\xffz") == 3);   /* malformed byte counts as one */

    /* encode */
    eq(utf8_encode(0x41, &a), "A");
    eq(utf8_encode(0xE9, &a), "\xc3\xa9");
    eq(utf8_encode(0x20AC, &a), "\xe2\x82\xac");
    eq(utf8_encode(0x1F600, &a), "\xf0\x9f\x98\x80");
    eq(utf8_encode(0xD800, &a), "\xef\xbf\xbd");      /* surrogate -> U+FFFD */
    eq(utf8_encode(0x110000, &a), "\xef\xbf\xbd");
    eq(utf8_encode(-5, &a), "\xef\xbf\xbd");
    assert(utf8_encoded_len(0x7F) == 1 && utf8_encoded_len(0x80) == 2 && utf8_encoded_len(0x7FF) == 2
           && utf8_encoded_len(0x800) == 3 && utf8_encoded_len(0xFFFF) == 3 && utf8_encoded_len(0x10000) == 4);

    /* properties */
    assert(cp_alpha_('a') && cp_alpha_(0xE9) && cp_alpha_(0x65E5) && cp_alpha_(0x3B1));
    assert(!cp_alpha_('1') && !cp_alpha_(' ') && !cp_alpha_(0x1F600));
    assert(cp_upper_('A') && !cp_upper_('a') && cp_lower_('a') && cp_upper_(0x391) && cp_lower_(0x3B1));
    assert(cp_digit_('7') && cp_digit_(0x0663) /* Arabic-indic 3 */ && !cp_digit_('a'));
    assert(cp_space_(' ') && cp_space_('\n') && cp_space_(0xA0) && cp_space_(0x3000) && !cp_space_('x'));
    assert(cp_combining_(0x301) && !cp_combining_('a'));
    assert(cp_control_(7) && cp_control_(0x85) && !cp_control_('a'));
    assert(cp_punct_('!') && cp_punct_(0x3002) && !cp_punct_('a'));
    assert(cp_wide_(0x65E5) && cp_wide_(0xFF21) && cp_wide_(0x1F600) && !cp_wide_('a') && !cp_wide_(0xE9));

    /* case */
    assert(cp_lower('A') == 'a' && cp_upper('z') == 'Z' && cp_lower('1') == '1');
    assert(cp_lower(0xC9) == 0xE9 && cp_upper(0xE9) == 0xC9);
    assert(cp_lower(0x391) == 0x3B1 && cp_upper(0x3B1) == 0x391);   /* Greek alpha */
    assert(cp_lower(0x416) == 0x436 && cp_upper(0x436) == 0x416);   /* Cyrillic zhe */
    eq(utf8_lower("HELLO \xc3\x89\xce\x91", &a), "hello \xc3\xa9\xce\xb1");
    eq(utf8_upper("hello \xc3\xa9\xce\xb1", &a), "HELLO \xc3\x89\xce\x91");
    /* length-changing mapping: Kelvin sign U+212A (3 bytes) lowercases to 'k' (1 byte) */
    eq(utf8_lower("\xe2\x84\xaa", &a), "k");
    eq(utf8_lower("a\xff" "B", &a), "a\xff" "b");     /* malformed byte passes through */
    assert(utf8_eq_ci_("Stra\xc3\x9f" "e", "STRA\xc3\x9f" "E") == 1);
    assert(utf8_eq_ci_("\xc3\x89" "cole", "\xc3\xa9" "COLE") == 1);
    assert(utf8_eq_ci_("abc", "abd") == 0);
    assert(utf8_eq_ci_("abc", "ab") == 0);

    /* width: ascii 1 each, CJK 2, combining 0, emoji 2, zero-width space 0 */
    assert(utf8_width("abc") == 3);
    assert(utf8_width("\xe6\x97\xa5\xe6\x9c\xac") == 4);
    assert(utf8_width("e\xcc\x81") == 1);              /* e + U+0301 */
    assert(utf8_width("\xf0\x9f\x98\x80") == 2);
    assert(utf8_width("a\xe2\x80\x8b" "b") == 2);      /* U+200B */
    assert(cp_width(0) == 0 && cp_width('a') == 1 && cp_width(0x65E5) == 2 && cp_width(9) == 0);

    printf("unicode OK\n");
    return 0;
}
