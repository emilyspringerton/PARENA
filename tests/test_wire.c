/* tests/test_wire.c -- stdlib/net/wire.prn 真實端對端驗證(kanban 優先佇列卡片
 * PCAP-0022)。刻意包含 >=128 的位元組(0xaa/0xff/192),驗證 raw-byte 的
 * bit-and 255 修正確實有效,不是只測小數值僥倖過關。 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_wire_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* read-u16-be:0x01 0x02 -> 258 */
    char u16buf[2] = { (char)0x01, (char)0x02 };
    assert(read_u16_be(u16buf, 0) == 258);

    /* read-u32-be:0x01 0x02 0x03 0x04 -> 16909060 */
    char u32buf[4] = { (char)0x01, (char)0x02, (char)0x03, (char)0x04 };
    assert(read_u32_be(u32buf, 0) == 16909060);

    /* mac-to-string:含 0xaa/0xff 這種 >=128 的位元組,真正考驗 raw-byte 修正 */
    char macbuf[6] = { (char)0xaa, (char)0xbb, (char)0xcc, (char)0xdd, (char)0xee, (char)0xff };
    char *mac = mac_to_string(macbuf, 0, &arena);
    assert(strcmp(mac, "aa:bb:cc:dd:ee:ff") == 0);

    /* ipv4-to-string:192.168.1.1,192 也 >=128 */
    char ipbuf[4] = { (char)192, (char)168, (char)1, (char)1 };
    char *ip = ipv4_to_string(ipbuf, 0, &arena);
    assert(strcmp(ip, "192.168.1.1") == 0);

    /* 邊界案例:全零/全 0xff */
    char zerobuf[4] = { 0, 0, 0, 0 };
    assert(strcmp(ipv4_to_string(zerobuf, 0, &arena), "0.0.0.0") == 0);
    char ffbuf[6] = { (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff, (char)0xff };
    assert(strcmp(mac_to_string(ffbuf, 0, &arena), "ff:ff:ff:ff:ff:ff") == 0);

    printf("test_wire: 全部通過\n");
    return 0;
}
