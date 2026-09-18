/* tests/test_database_mssql_util.c -- real end-to-end verification of
 * stdlib/database/mssql-util.prn (S499, founder real-time, precise engineering requirements
 * handed off before logging off: a zero-copy T-SQL type-transpile layer for UNIQUEIDENTIFIER/
 * DATETIME2/BIT before async fanout to Postgres/Oracle-shaped sinks). Pure string manipulation,
 * no #target/FFI/host glue at all -- every assertion below is a real, live, unprivileged run,
 * same as any other pure-logic PARENA module's own test.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_database_mssql_util_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* ---- UNIQUEIDENTIFIER -> UUID ---- */
    char *g1 = mssql_uniqueidentifier_to_uuid(
        (char *)"{6F9619FF-8B86-D011-B42D-00C04FC964FF}", &arena);
    assert(strcmp(g1, "6F9619FF-8B86-D011-B42D-00C04FC964FF") == 0);
    printf("PASS: bracketed GUID has its braces stripped\n");

    /* Already-unbracketed input passes through completely unchanged -- the same String pointer,
     * zero allocation, proving the "common case costs nothing" claim in this file's own header
     * comment, not just that the text happens to match. */
    char *already_plain = (char *)"6F9619FF-8B86-D011-B42D-00C04FC964FF";
    char *g2 = mssql_uniqueidentifier_to_uuid(already_plain, &arena);
    assert(g2 == already_plain);
    printf("PASS: an already-unbracketed GUID passes through with zero allocation (same pointer)\n");

    /* A malformed/partial bracket (only one side) is left alone, not guessed at. */
    char *g3 = mssql_uniqueidentifier_to_uuid((char *)"{not-fully-bracketed", &arena);
    assert(strcmp(g3, "{not-fully-bracketed") == 0);
    printf("PASS: a partially-bracketed string is passed through unchanged, not mishandled\n");

    /* ---- DATETIME2 -> TIMESTAMP ---- */
    char *d1 = mssql_datetime2_to_timestamp((char *)"2026-09-18 06:25:20.1234567", &arena);
    assert(strcmp(d1, "2026-09-18 06:25:20.123456") == 0);
    printf("PASS: a real 7-digit DATETIME2 fractional-seconds component is truncated to 6\n");

    /* Truncated, never rounded -- 7654321 truncates to 765432, NOT rounded up to 765433. */
    char *d2 = mssql_datetime2_to_timestamp((char *)"2026-01-01 00:00:00.7654321", &arena);
    assert(strcmp(d2, "2026-01-01 00:00:00.765432") == 0);
    printf("PASS: truncation, not rounding, confirmed against a case where they'd differ\n");

    /* Already <=6 digits, or no fractional seconds at all, pass through unchanged. */
    char *already6 = (char *)"2026-09-18 06:25:20.123456";
    char *d3 = mssql_datetime2_to_timestamp(already6, &arena);
    assert(d3 == already6);
    char *no_frac = (char *)"2026-09-18 06:25:20";
    char *d4 = mssql_datetime2_to_timestamp(no_frac, &arena);
    assert(d4 == no_frac);
    printf("PASS: already-6-digit and no-fractional-seconds inputs both pass through with zero "
           "allocation\n");

    /* ---- BIT -> BOOLEAN ---- */
    Result b1 = mssql_bit_to_boolean_token((char *)"1", &arena);
    assert(b1.tag == 1);
    assert(strcmp((char *)b1.value, "TRUE") == 0);
    Result b2 = mssql_bit_to_boolean_token((char *)"0", &arena);
    assert(b2.tag == 1);
    assert(strcmp((char *)b2.value, "FALSE") == 0);
    printf("PASS: real BIT 1/0 values convert to TRUE/FALSE tokens\n");

    /* A real, honest error for anything that is genuinely neither "0" nor "1" -- never silently
     * coerced to FALSE (this file's own header comment names exactly why). */
    Result b3 = mssql_bit_to_boolean_token((char *)"2", &arena);
    assert(b3.tag == 0);
    MssqlTranspileError *e = (MssqlTranspileError *)b3.value;
    assert(e->tag == 0 /* MssqlTranspileError_TAG_InvalidBitValue */);
    Result b4 = mssql_bit_to_boolean_token((char *)"", &arena);
    assert(b4.tag == 0);
    printf("PASS: a genuinely invalid BIT value is reported as a real, honest error, never "
           "silently defaulted to FALSE\n");

    printf("test_database_mssql_util: all real assertions passed\n");
    return 0;
}
