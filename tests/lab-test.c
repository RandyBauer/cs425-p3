#include "../src/lab.h"
#include "harness/unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Built from four octets, so the expected values read like the addresses. */
#define IP(a, b, c, d)                                                         \
  ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* A value no parser should ever write, to prove "untouched on failure". */
#define SENTINEL 0xDEADBEEFu

/* The forwarding table from the handout, also saved as tables/example.txt. */
static const char *EXAMPLE_TABLE =
    "# destination      gateway          device\n"
    "default            132.178.227.1    eno1\n"
    "132.178.227.0/25   direct           eno1\n"
    "10.0.0.0/8         10.1.0.1         eth1\n"
    "10.1.0.0/16        direct           eth1\n"
    "10.1.2.0/24        direct           eth2\n"
    "10.1.2.128/25      10.1.2.1         eth2   # a router behind eth2\n"
    "192.168.0.0/16     10.1.0.1         eth1\n";

void setUp(void) {}

void tearDown(void) {}

/* Parse CIDR text that the test knows is valid. */
static cidr_t block_of(const char *text)
{
  cidr_t block = {0, 0};
  TEST_ASSERT_TRUE_MESSAGE(cidr_parse(text, &block), text);
  return block;
}

/* Parse a route line that the test knows is valid. */
static route_t route_of(const char *line)
{
  route_t route;
  memset(&route, 0, sizeof route);
  TEST_ASSERT_TRUE_MESSAGE(route_parse(line, &route), line);
  return route;
}

/* Write text to a temporary file and rewind it, ready for table_load(). */
static FILE *file_of(const char *text)
{
  FILE *f = tmpfile();
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(fputs(text, f) >= 0);
  rewind(f);
  return f;
}

/* Load text into a new table, which must succeed. */
static fwd_table_t *table_of(const char *text)
{
  fwd_table_t *table = table_create();
  FILE *f = file_of(text);
  size_t bad = SENTINEL;
  TEST_ASSERT_NOT_NULL(table);
  TEST_ASSERT_TRUE(table_load(table, f, &bad));
  TEST_ASSERT_EQUAL_size_t(0, bad);
  fclose(f);
  return table;
}

/* Look up an address given as text and return the route's device, or NULL. */
static const char *dev_for(const fwd_table_t *table, const char *addr_text)
{
  uint32_t addr;
  const route_t *route;
  TEST_ASSERT_TRUE(ip_parse(addr_text, &addr));
  route = table_lookup(table, addr);
  return route == NULL ? NULL : route->dev;
}

/* ------------------------------------------------------------------------
 * Task 1 - ip_parse, ip_format, cidr_parse, cidr_format
 * --------------------------------------------------------------------- */

void test_ip_parse_valid(void)
{
  uint32_t addr = SENTINEL;
  TEST_ASSERT_TRUE(ip_parse("192.168.1.10", &addr));
  TEST_ASSERT_EQUAL_HEX32(0xC0A8010Au, addr);
  TEST_ASSERT_TRUE(ip_parse("132.178.227.11", &addr));
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 11), addr);
  TEST_ASSERT_TRUE(ip_parse("10.0.0.1", &addr));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 1), addr);
}

void test_ip_parse_boundaries(void)
{
  uint32_t addr = SENTINEL;
  TEST_ASSERT_TRUE(ip_parse("0.0.0.0", &addr));
  TEST_ASSERT_EQUAL_HEX32(0u, addr);
  TEST_ASSERT_TRUE(ip_parse("255.255.255.255", &addr));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, addr);
  TEST_ASSERT_TRUE(ip_parse("1.0.0.0", &addr));
  TEST_ASSERT_EQUAL_HEX32(IP(1, 0, 0, 0), addr);
  TEST_ASSERT_TRUE(ip_parse("0.0.0.1", &addr));
  TEST_ASSERT_EQUAL_HEX32(1u, addr);
}

/* Every string the handout lists, plus the cases its wording covers. */
void test_ip_parse_rejects_malformed(void)
{
  static const char *bad[] = {
      "1.2.3",        "1.2.3.4.5",   "1..2.3",     "256.1.1.1",
      "010.0.0.1",    "1.2.3.4x",    "",           " 1.2.3.4",
      "1.2.3.4 ",     "+1.2.3.4",    "-1.2.3.4",   "1.2.3.",
      ".1.2.3",       "00.0.0.0",    "1.2.3.04",   "99999999999.0.0.0",
      "1.2.3.256",    "1.2.3.4\n",   "a.b.c.d",    "1.2.3.4/24",
      "1,2,3,4",      "4294967296.0.0.0",
  };
  for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
    uint32_t addr = SENTINEL;
    TEST_ASSERT_FALSE_MESSAGE(ip_parse(bad[k], &addr), bad[k]);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(SENTINEL, addr, bad[k]);
  }
}

void test_ip_parse_null(void)
{
  uint32_t addr = SENTINEL;
  TEST_ASSERT_FALSE(ip_parse(NULL, &addr));
  TEST_ASSERT_EQUAL_HEX32(SENTINEL, addr);
  TEST_ASSERT_FALSE(ip_parse("1.2.3.4", NULL));
}

void test_ip_format(void)
{
  char out[IP_STR_LEN];
  ip_format(0xC0A8010Au, out);
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", out);
  ip_format(0, out);
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", out);
  ip_format(0xFFFFFFFFu, out);
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", out);
  ip_format(IP(10, 0, 20, 3), out);
  TEST_ASSERT_EQUAL_STRING("10.0.20.3", out);
}

void test_ip_round_trip(void)
{
  static const char *good[] = {"0.0.0.0", "8.8.8.8", "132.178.227.127",
                               "255.255.255.255", "10.200.0.99"};
  for (size_t k = 0; k < sizeof good / sizeof good[0]; k++) {
    uint32_t addr;
    char out[IP_STR_LEN];
    TEST_ASSERT_TRUE(ip_parse(good[k], &addr));
    ip_format(addr, out);
    TEST_ASSERT_EQUAL_STRING(good[k], out);
  }
}

void test_cidr_parse_valid(void)
{
  cidr_t block = {SENTINEL, 99};
  TEST_ASSERT_TRUE(cidr_parse("192.168.1.0/24", &block));
  TEST_ASSERT_EQUAL_HEX32(IP(192, 168, 1, 0), block.addr);
  TEST_ASSERT_EQUAL_UINT8(24, block.prefix);

  /* Host bits may be set: this is an interface address. */
  TEST_ASSERT_TRUE(cidr_parse("132.178.227.11/25", &block));
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 11), block.addr);
  TEST_ASSERT_EQUAL_UINT8(25, block.prefix);
}

void test_cidr_parse_boundaries(void)
{
  cidr_t block;
  TEST_ASSERT_TRUE(cidr_parse("0.0.0.0/0", &block));
  TEST_ASSERT_EQUAL_HEX32(0u, block.addr);
  TEST_ASSERT_EQUAL_UINT8(0, block.prefix);
  TEST_ASSERT_TRUE(cidr_parse("255.255.255.255/32", &block));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, block.addr);
  TEST_ASSERT_EQUAL_UINT8(32, block.prefix);
  TEST_ASSERT_TRUE(cidr_parse("10.0.0.1/31", &block));
  TEST_ASSERT_EQUAL_UINT8(31, block.prefix);
  TEST_ASSERT_TRUE(cidr_parse("10.0.0.1/9", &block));
  TEST_ASSERT_EQUAL_UINT8(9, block.prefix);
}

void test_cidr_parse_rejects_malformed(void)
{
  static const char *bad[] = {
      "1.2.3.4/33",  "1.2.3.4/024",  "1.2.3.4/",    "1.2.3/24",
      "1.2.3.4",     "1.2.3.4/00",   "1.2.3.4/+5",  "1.2.3.4/ 5",
      "1.2.3.4/5 ",  "1.2.3.4//5",   "1.2.3.4/5/6", "1.2.3.4/-1",
      "1.2.3.4/x",   "/24",          "",            "256.0.0.0/8",
      "010.0.0.0/8", "1.2.3.4x/24",  "1.2.3.4/100", "1.2.3.4/99999999999",
      " 1.2.3.4/8",  "1.2.3.4 /8",
  };
  for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
    cidr_t block = {SENTINEL, 77};
    TEST_ASSERT_FALSE_MESSAGE(cidr_parse(bad[k], &block), bad[k]);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(SENTINEL, block.addr, bad[k]);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(77, block.prefix, bad[k]);
  }
}

void test_cidr_parse_null(void)
{
  cidr_t block = {SENTINEL, 77};
  TEST_ASSERT_FALSE(cidr_parse(NULL, &block));
  TEST_ASSERT_EQUAL_HEX32(SENTINEL, block.addr);
  TEST_ASSERT_FALSE(cidr_parse("1.2.3.0/24", NULL));
}

void test_cidr_format(void)
{
  char out[CIDR_STR_LEN];
  cidr_t block = {IP(192, 168, 1, 64), 26};
  cidr_format(block, out);
  TEST_ASSERT_EQUAL_STRING("192.168.1.64/26", out);

  /* The address is written exactly as stored, host bits and all. */
  block.addr = IP(132, 178, 227, 11);
  block.prefix = 25;
  cidr_format(block, out);
  TEST_ASSERT_EQUAL_STRING("132.178.227.11/25", out);
}

void test_cidr_format_boundaries(void)
{
  char out[CIDR_STR_LEN];
  cidr_t block = {0, 0};
  cidr_format(block, out);
  TEST_ASSERT_EQUAL_STRING("0.0.0.0/0", out);

  /* The longest possible string just fits. */
  block.addr = 0xFFFFFFFFu;
  block.prefix = 32;
  cidr_format(block, out);
  TEST_ASSERT_EQUAL_STRING("255.255.255.255/32", out);

  /* A prefix over 32 is written as 32. */
  block.prefix = 40;
  cidr_format(block, out);
  TEST_ASSERT_EQUAL_STRING("255.255.255.255/32", out);
  block.prefix = 255;
  cidr_format(block, out);
  TEST_ASSERT_EQUAL_STRING("255.255.255.255/32", out);
}

/* ------------------------------------------------------------------------
 * Task 2 - Subnet arithmetic
 * --------------------------------------------------------------------- */

void test_cidr_mask(void)
{
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF00u, cidr_mask(24));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF80u, cidr_mask(25));
  TEST_ASSERT_EQUAL_HEX32(0xFF000000u, cidr_mask(8));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, cidr_mask(1));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, cidr_mask(31));
}

void test_cidr_mask_boundaries(void)
{
  /* The plain shift gives 0xFFFFFFFF here on x86. */
  TEST_ASSERT_EQUAL_HEX32(0u, cidr_mask(0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_mask(32));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_mask(33));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_mask(255));
}

void test_cidr_mask_every_prefix(void)
{
  /* Every mask is prefix ones followed by zeros. */
  for (uint8_t p = 0; p <= 32; p++) {
    uint32_t mask = cidr_mask(p);
    uint32_t ones = 0;
    for (uint32_t m = mask; m != 0; m <<= 1) {
      TEST_ASSERT_TRUE(m & 0x80000000u);
      ones++;
    }
    TEST_ASSERT_EQUAL_UINT32(p, ones);
  }
}

/* The Onyx numbers from A4, which the handout's info transcript shows. */
void test_onyx_subnet(void)
{
  cidr_t onyx = block_of("132.178.227.11/25");
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 0), cidr_network(onyx));
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 127), cidr_broadcast(onyx));
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 1), cidr_first_host(onyx));
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 126), cidr_last_host(onyx));
  TEST_ASSERT_EQUAL_UINT64(126, cidr_host_count(onyx));
}

void test_cidr_ranges_ordinary(void)
{
  cidr_t block = block_of("10.20.30.40/16");
  TEST_ASSERT_EQUAL_HEX32(IP(10, 20, 0, 0), cidr_network(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 20, 255, 255), cidr_broadcast(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 20, 0, 1), cidr_first_host(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 20, 255, 254), cidr_last_host(block));
  TEST_ASSERT_EQUAL_UINT64(65534, cidr_host_count(block));

  block = block_of("192.168.1.5/30");
  TEST_ASSERT_EQUAL_HEX32(IP(192, 168, 1, 4), cidr_network(block));
  TEST_ASSERT_EQUAL_HEX32(IP(192, 168, 1, 7), cidr_broadcast(block));
  TEST_ASSERT_EQUAL_HEX32(IP(192, 168, 1, 5), cidr_first_host(block));
  TEST_ASSERT_EQUAL_HEX32(IP(192, 168, 1, 6), cidr_last_host(block));
  TEST_ASSERT_EQUAL_UINT64(2, cidr_host_count(block));
}

void test_cidr_ranges_slash0(void)
{
  cidr_t block = block_of("1.2.3.4/0");
  TEST_ASSERT_EQUAL_HEX32(0u, cidr_network(block));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_broadcast(block));
  TEST_ASSERT_EQUAL_HEX32(IP(0, 0, 0, 1), cidr_first_host(block));
  TEST_ASSERT_EQUAL_HEX32(IP(255, 255, 255, 254), cidr_last_host(block));
  /* 2^32 - 2: wraps to a small number if done in 32 bits. */
  TEST_ASSERT_EQUAL_UINT64(4294967294u, cidr_host_count(block));
}

void test_cidr_ranges_slash31(void)
{
  /* RFC 3021: both addresses are hosts, no network or broadcast. */
  cidr_t block = block_of("10.0.0.7/31");
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 6), cidr_network(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_broadcast(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 6), cidr_first_host(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_last_host(block));
  TEST_ASSERT_EQUAL_UINT64(2, cidr_host_count(block));
}

void test_cidr_ranges_slash32(void)
{
  cidr_t block = block_of("10.0.0.7/32");
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_network(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_broadcast(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_first_host(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_last_host(block));
  /* Not 4294967295, which is 2^0 - 2 wrapped. */
  TEST_ASSERT_EQUAL_UINT64(1, cidr_host_count(block));
}

void test_cidr_ranges_extreme_addresses(void)
{
  cidr_t top = block_of("255.255.255.255/32");
  cidr_t bottom = block_of("0.0.0.0/32");
  cidr_t top31 = block_of("255.255.255.254/31");
  cidr_t bottom31 = block_of("0.0.0.0/31");
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_first_host(top));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_last_host(top));
  TEST_ASSERT_EQUAL_HEX32(0u, cidr_first_host(bottom));
  TEST_ASSERT_EQUAL_HEX32(0u, cidr_last_host(bottom));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, cidr_first_host(top31));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, cidr_last_host(top31));
  TEST_ASSERT_EQUAL_HEX32(0u, cidr_first_host(bottom31));
  TEST_ASSERT_EQUAL_HEX32(1u, cidr_last_host(bottom31));
}

void test_cidr_host_count_every_prefix(void)
{
  for (uint8_t p = 0; p <= 30; p++) {
    cidr_t block = {0, p};
    uint64_t expect = ((uint64_t)1 << (32 - p)) - 2;
    TEST_ASSERT_EQUAL_UINT64(expect, cidr_host_count(block));
  }
}

void test_cidr_prefix_over_32_is_32(void)
{
  cidr_t block = {IP(10, 0, 0, 7), 40};
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_network(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_broadcast(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_first_host(block));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), cidr_last_host(block));
  TEST_ASSERT_EQUAL_UINT64(1, cidr_host_count(block));
  TEST_ASSERT_TRUE(cidr_contains(block, IP(10, 0, 0, 7)));
  TEST_ASSERT_FALSE(cidr_contains(block, IP(10, 0, 0, 6)));
  TEST_ASSERT_TRUE(cidr_is_network(block));
}

void test_cidr_contains(void)
{
  cidr_t onyx = block_of("132.178.227.11/25");
  TEST_ASSERT_TRUE(cidr_contains(onyx, IP(132, 178, 227, 50)));
  TEST_ASSERT_FALSE(cidr_contains(onyx, IP(132, 178, 227, 200)));
  TEST_ASSERT_TRUE(cidr_contains(onyx, IP(132, 178, 227, 0)));
  TEST_ASSERT_TRUE(cidr_contains(onyx, IP(132, 178, 227, 127)));
  TEST_ASSERT_FALSE(cidr_contains(onyx, IP(132, 178, 227, 128)));
  TEST_ASSERT_FALSE(cidr_contains(onyx, IP(132, 178, 226, 255)));
}

void test_cidr_contains_boundaries(void)
{
  cidr_t all = block_of("0.0.0.0/0");
  cidr_t one = block_of("10.0.0.7/32");
  cidr_t pair = block_of("10.0.0.6/31");
  /* /0 contains everything; this is what makes the default route work. */
  TEST_ASSERT_TRUE(cidr_contains(all, 0u));
  TEST_ASSERT_TRUE(cidr_contains(all, 0xFFFFFFFFu));
  TEST_ASSERT_TRUE(cidr_contains(all, IP(8, 8, 8, 8)));
  TEST_ASSERT_TRUE(cidr_contains(one, IP(10, 0, 0, 7)));
  TEST_ASSERT_FALSE(cidr_contains(one, IP(10, 0, 0, 6)));
  TEST_ASSERT_TRUE(cidr_contains(pair, IP(10, 0, 0, 6)));
  TEST_ASSERT_TRUE(cidr_contains(pair, IP(10, 0, 0, 7)));
  TEST_ASSERT_FALSE(cidr_contains(pair, IP(10, 0, 0, 8)));
}

void test_cidr_is_network(void)
{
  TEST_ASSERT_TRUE(cidr_is_network(block_of("192.168.1.0/24")));
  TEST_ASSERT_FALSE(cidr_is_network(block_of("192.168.1.5/24")));
  TEST_ASSERT_FALSE(cidr_is_network(block_of("132.178.227.11/25")));
  TEST_ASSERT_TRUE(cidr_is_network(block_of("132.178.227.0/25")));
  TEST_ASSERT_TRUE(cidr_is_network(block_of("0.0.0.0/0")));
  TEST_ASSERT_FALSE(cidr_is_network(block_of("0.0.0.1/0")));
  TEST_ASSERT_TRUE(cidr_is_network(block_of("10.0.0.6/31")));
  TEST_ASSERT_FALSE(cidr_is_network(block_of("10.0.0.7/31")));
  TEST_ASSERT_TRUE(cidr_is_network(block_of("10.0.0.7/32")));
  TEST_ASSERT_TRUE(cidr_is_network(block_of("255.255.255.255/32")));
}

void test_cidr_subnet_handout_example(void)
{
  static const char *expect[] = {"192.168.1.0/26", "192.168.1.64/26",
                                 "192.168.1.128/26", "192.168.1.192/26"};
  cidr_t block = block_of("192.168.1.0/24");
  for (uint32_t i = 0; i < 4; i++) {
    cidr_t sub;
    char out[CIDR_STR_LEN];
    TEST_ASSERT_TRUE(cidr_subnet(block, 4, i, &sub));
    cidr_format(sub, out);
    TEST_ASSERT_EQUAL_STRING(expect[i], out);
  }
}

void test_cidr_subnet_one_is_the_block(void)
{
  cidr_t sub;
  TEST_ASSERT_TRUE(cidr_subnet(block_of("10.0.0.0/8"), 1, 0, &sub));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 0), sub.addr);
  TEST_ASSERT_EQUAL_UINT8(8, sub.prefix);

  /* /0 into 1 would shift by 32 in 32 bit arithmetic. */
  TEST_ASSERT_TRUE(cidr_subnet(block_of("0.0.0.0/0"), 1, 0, &sub));
  TEST_ASSERT_EQUAL_HEX32(0u, sub.addr);
  TEST_ASSERT_EQUAL_UINT8(0, sub.prefix);

  TEST_ASSERT_TRUE(cidr_subnet(block_of("10.0.0.7/32"), 1, 0, &sub));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), sub.addr);
  TEST_ASSERT_EQUAL_UINT8(32, sub.prefix);
}

void test_cidr_subnet_boundaries(void)
{
  cidr_t sub;
  cidr_t all = block_of("0.0.0.0/0");

  /* /0 into 2 gives the two halves of the address space. */
  TEST_ASSERT_TRUE(cidr_subnet(all, 2, 0, &sub));
  TEST_ASSERT_EQUAL_HEX32(0u, sub.addr);
  TEST_ASSERT_EQUAL_UINT8(1, sub.prefix);
  TEST_ASSERT_TRUE(cidr_subnet(all, 2, 1, &sub));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, sub.addr);
  TEST_ASSERT_EQUAL_UINT8(1, sub.prefix);

  /* /0 into 2^31: the largest n a uint32_t holds; the last is a /31. */
  TEST_ASSERT_TRUE(cidr_subnet(all, 0x80000000u, 0x7FFFFFFFu, &sub));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEu, sub.addr);
  TEST_ASSERT_EQUAL_UINT8(31, sub.prefix);

  /* /31 into 2 gives two /32 hosts; /30 into 4 the same. */
  TEST_ASSERT_TRUE(cidr_subnet(block_of("10.0.0.6/31"), 2, 1, &sub));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), sub.addr);
  TEST_ASSERT_EQUAL_UINT8(32, sub.prefix);
  TEST_ASSERT_TRUE(cidr_subnet(block_of("255.255.255.252/30"), 4, 3, &sub));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, sub.addr);
  TEST_ASSERT_EQUAL_UINT8(32, sub.prefix);
}

void test_cidr_subnet_covers_block_in_order(void)
{
  /* 16 subnets of a /20, each a /24, adjacent and lowest first. */
  cidr_t block = block_of("172.16.0.0/20");
  for (uint32_t i = 0; i < 16; i++) {
    cidr_t sub;
    TEST_ASSERT_TRUE(cidr_subnet(block, 16, i, &sub));
    TEST_ASSERT_EQUAL_UINT8(24, sub.prefix);
    TEST_ASSERT_EQUAL_HEX32(IP(172, 16, 0, 0) + (i << 8), sub.addr);
    TEST_ASSERT_TRUE(cidr_is_network(sub));
  }
}

void test_cidr_subnet_rejects(void)
{
  cidr_t sub = {SENTINEL, 77};
  cidr_t block = block_of("192.168.1.0/24");

  TEST_ASSERT_FALSE(cidr_subnet(block, 4, 0, NULL));
  /* Not a network. */
  TEST_ASSERT_FALSE(cidr_subnet(block_of("192.168.1.5/24"), 4, 0, &sub));
  /* Not a power of two, and 0 is not one either. */
  TEST_ASSERT_FALSE(cidr_subnet(block, 0, 0, &sub));
  TEST_ASSERT_FALSE(cidr_subnet(block, 3, 0, &sub));
  TEST_ASSERT_FALSE(cidr_subnet(block, 6, 0, &sub));
  TEST_ASSERT_FALSE(cidr_subnet(block, 0xFFFFFFFFu, 0, &sub));
  /* The new prefix would pass 32. */
  TEST_ASSERT_FALSE(cidr_subnet(block, 512, 0, &sub));
  TEST_ASSERT_FALSE(cidr_subnet(block_of("10.0.0.7/32"), 2, 0, &sub));
  TEST_ASSERT_FALSE(cidr_subnet(block_of("10.0.0.6/31"), 4, 0, &sub));
  /* i out of range. */
  TEST_ASSERT_FALSE(cidr_subnet(block, 4, 4, &sub));
  TEST_ASSERT_FALSE(cidr_subnet(block, 1, 1, &sub));

  TEST_ASSERT_EQUAL_HEX32(SENTINEL, sub.addr);
  TEST_ASSERT_EQUAL_UINT8(77, sub.prefix);
}

void test_cidr_subnet_prefix_over_32(void)
{
  /* Treated as a /32: only n == 1 is possible, and the result says /32. */
  cidr_t block = {IP(10, 0, 0, 7), 40};
  cidr_t sub;
  TEST_ASSERT_TRUE(cidr_subnet(block, 1, 0, &sub));
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 7), sub.addr);
  TEST_ASSERT_EQUAL_UINT8(32, sub.prefix);
  TEST_ASSERT_FALSE(cidr_subnet(block, 2, 0, &sub));
}

/* ------------------------------------------------------------------------
 * Task 3 - route_parse and the forwarding table
 * --------------------------------------------------------------------- */

void test_route_parse_gateway(void)
{
  route_t r = route_of("10.0.0.0/8         10.1.0.1         eth1");
  TEST_ASSERT_EQUAL_HEX32(IP(10, 0, 0, 0), r.dest.addr);
  TEST_ASSERT_EQUAL_UINT8(8, r.dest.prefix);
  TEST_ASSERT_EQUAL_HEX32(IP(10, 1, 0, 1), r.gateway);
  TEST_ASSERT_EQUAL_STRING("eth1", r.dev);
}

void test_route_parse_default_and_direct(void)
{
  route_t r = route_of("default 132.178.227.1 eno1");
  TEST_ASSERT_EQUAL_HEX32(0u, r.dest.addr);
  TEST_ASSERT_EQUAL_UINT8(0, r.dest.prefix);
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 1), r.gateway);
  TEST_ASSERT_EQUAL_STRING("eno1", r.dev);

  r = route_of("132.178.227.0/25 direct eno1");
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 0), r.dest.addr);
  TEST_ASSERT_EQUAL_UINT8(25, r.dest.prefix);
  TEST_ASSERT_EQUAL_HEX32(0u, r.gateway);

  /* 0.0.0.0/0 written out is the same as default. */
  r = route_of("0.0.0.0/0 direct lo");
  TEST_ASSERT_EQUAL_HEX32(0u, r.dest.addr);
  TEST_ASSERT_EQUAL_UINT8(0, r.dest.prefix);
  TEST_ASSERT_EQUAL_STRING("lo", r.dev);
}

void test_route_parse_whitespace(void)
{
  route_t r = route_of("  \t10.1.2.0/24\t\tdirect \t eth2  \t\n");
  TEST_ASSERT_EQUAL_HEX32(IP(10, 1, 2, 0), r.dest.addr);
  TEST_ASSERT_EQUAL_STRING("eth2", r.dev);

  /* A file edited on Windows: the \r is trailing whitespace. */
  r = route_of("10.1.2.0/24 direct eth2\r\n");
  TEST_ASSERT_EQUAL_STRING("eth2", r.dev);
}

void test_route_parse_device_length(void)
{
  /* DEV_NAME_LEN - 1 characters fit; one more does not. */
  route_t r = route_of("10.0.0.0/8 direct abcdefghijklmno");
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmno", r.dev);
  r = route_of("10.0.0.0/8 direct x");
  TEST_ASSERT_EQUAL_STRING("x", r.dev);
}

void test_route_parse_rejects(void)
{
  static const char *bad[] = {
      "",
      "   \n",
      "default",
      "default direct",
      "default direct eth0 extra",
      "default direct eth0 # comment",
      "10.1.2.5/24 direct eth2",
      "10.1.2.0 direct eth2",
      "10.1.2.0/33 direct eth2",
      "Default direct eth2",
      "default Direct eth2",
      "default 10.1.0 eth2",
      "default 010.1.0.1 eth2",
      "default 10.1.0.1/32 eth2",
      "10.0.0.0/8 direct abcdefghijklmnop",
      "10.0.0.0/8 direct et\rh0",
      "10.0.0.0/8,direct,eth0",
      "1.2.3.4.5/8 direct eth0",
      "255.255.255.255/32/32 direct eth0",
  };
  for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
    route_t r;
    memset(&r, 0xAB, sizeof r);
    TEST_ASSERT_FALSE_MESSAGE(route_parse(bad[k], &r), bad[k]);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xABABABABu, r.dest.addr, bad[k]);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xABABABABu, r.gateway, bad[k]);
  }
}

void test_route_parse_null(void)
{
  route_t r;
  TEST_ASSERT_FALSE(route_parse(NULL, &r));
  TEST_ASSERT_FALSE(route_parse("default direct eth0", NULL));
}

void test_table_create_destroy(void)
{
  fwd_table_t *table = table_create();
  TEST_ASSERT_NOT_NULL(table);
  TEST_ASSERT_EQUAL_size_t(0, table_count(table));
  TEST_ASSERT_NULL(table_lookup(table, IP(8, 8, 8, 8)));
  table_destroy(table);
  /* NULL is allowed and does nothing. */
  table_destroy(NULL);
}

void test_table_count_null(void)
{
  TEST_ASSERT_EQUAL_size_t(0, table_count(NULL));
}

void test_table_add(void)
{
  fwd_table_t *table = table_create();
  route_t r = route_of("10.0.0.0/8 10.1.0.1 eth1");
  TEST_ASSERT_TRUE(table_add(table, &r));
  TEST_ASSERT_EQUAL_size_t(1, table_count(table));

  /* The table holds a copy, so changing ours does not change it. */
  r.gateway = 0;
  strcpy(r.dev, "changed");
  const route_t *got = table_lookup(table, IP(10, 9, 9, 9));
  TEST_ASSERT_NOT_NULL(got);
  TEST_ASSERT_EQUAL_HEX32(IP(10, 1, 0, 1), got->gateway);
  TEST_ASSERT_EQUAL_STRING("eth1", got->dev);
  table_destroy(table);
}

void test_table_add_rejects(void)
{
  fwd_table_t *table = table_create();
  route_t r = route_of("10.0.0.0/8 10.1.0.1 eth1");
  route_t host_bits = r;
  route_t same_block = route_of("10.0.0.0/8 direct eth9");
  host_bits.dest.addr = IP(10, 0, 0, 1);

  TEST_ASSERT_FALSE(table_add(NULL, &r));
  TEST_ASSERT_FALSE(table_add(table, NULL));
  TEST_ASSERT_FALSE(table_add(table, &host_bits));
  TEST_ASSERT_EQUAL_size_t(0, table_count(table));

  TEST_ASSERT_TRUE(table_add(table, &r));
  /* A second route to the same block, whatever its gateway or device. */
  TEST_ASSERT_FALSE(table_add(table, &r));
  TEST_ASSERT_FALSE(table_add(table, &same_block));
  TEST_ASSERT_EQUAL_size_t(1, table_count(table));
  TEST_ASSERT_EQUAL_STRING("eth1", dev_for(table, "10.1.1.1"));
  table_destroy(table);
}

void test_table_add_two_defaults(void)
{
  fwd_table_t *table = table_create();
  route_t a = route_of("default 132.178.227.1 eno1");
  route_t b = route_of("0.0.0.0/0 10.0.0.1 eth0");
  TEST_ASSERT_TRUE(table_add(table, &a));
  TEST_ASSERT_FALSE(table_add(table, &b));
  TEST_ASSERT_EQUAL_size_t(1, table_count(table));
  table_destroy(table);
}

void test_table_add_same_network_other_prefix(void)
{
  /* 10.0.0.0/8 and 10.0.0.0/16 are different blocks. */
  fwd_table_t *table = table_create();
  route_t a = route_of("10.0.0.0/8 direct eth1");
  route_t b = route_of("10.0.0.0/16 direct eth2");
  route_t c = route_of("10.0.0.0/32 direct eth3");
  route_t d = c;
  d.dest.prefix = 40; /* treated as /32, so the same block as c */
  TEST_ASSERT_TRUE(table_add(table, &a));
  TEST_ASSERT_TRUE(table_add(table, &b));
  TEST_ASSERT_TRUE(table_add(table, &c));
  TEST_ASSERT_FALSE(table_add(table, &d));
  TEST_ASSERT_EQUAL_size_t(3, table_count(table));
  table_destroy(table);
}

void test_table_add_grows(void)
{
  /* Far past the first allocation, so realloc() moves the array. */
  fwd_table_t *table = table_create();
  for (uint32_t k = 0; k < 1000; k++) {
    route_t r;
    memset(&r, 0, sizeof r);
    r.dest.addr = IP(10, 0, 0, 0) + (k << 8);
    r.dest.prefix = 24;
    r.gateway = k;
    snprintf(r.dev, sizeof r.dev, "eth%u", (unsigned)k);
    TEST_ASSERT_TRUE(table_add(table, &r));
  }
  TEST_ASSERT_EQUAL_size_t(1000, table_count(table));
  for (uint32_t k = 0; k < 1000; k++) {
    const route_t *got = table_lookup(table, IP(10, 0, 0, 77) + (k << 8));
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_UINT32(k, got->gateway);
  }
  table_destroy(table);
}

void test_table_lookup_longest_prefix(void)
{
  fwd_table_t *table = table_of(EXAMPLE_TABLE);
  const route_t *r;
  TEST_ASSERT_EQUAL_size_t(7, table_count(table));

  /* The four lookups in the handout's transcript. */
  r = table_lookup(table, IP(132, 178, 227, 50));
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_HEX32(0u, r->gateway);
  TEST_ASSERT_EQUAL_STRING("eno1", r->dev);

  r = table_lookup(table, IP(8, 8, 8, 8));
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_UINT8(0, r->dest.prefix);
  TEST_ASSERT_EQUAL_HEX32(IP(132, 178, 227, 1), r->gateway);
  TEST_ASSERT_EQUAL_STRING("eno1", r->dev);

  r = table_lookup(table, IP(10, 1, 2, 5));
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_UINT8(24, r->dest.prefix);
  TEST_ASSERT_EQUAL_HEX32(0u, r->gateway);
  TEST_ASSERT_EQUAL_STRING("eth2", r->dev);

  /* Inside the /8, the /16, the /24 and the /25: the /25 wins. */
  r = table_lookup(table, IP(10, 1, 2, 200));
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_UINT8(25, r->dest.prefix);
  TEST_ASSERT_EQUAL_HEX32(IP(10, 1, 2, 1), r->gateway);
  TEST_ASSERT_EQUAL_STRING("eth2", r->dev);

  /* One level up at a time. */
  r = table_lookup(table, IP(10, 1, 3, 1));
  TEST_ASSERT_EQUAL_UINT8(16, r->dest.prefix);
  r = table_lookup(table, IP(10, 2, 0, 1));
  TEST_ASSERT_EQUAL_UINT8(8, r->dest.prefix);
  /* Onyx's own /25 stops at .127, so .200 goes to the default route. */
  r = table_lookup(table, IP(132, 178, 227, 200));
  TEST_ASSERT_EQUAL_UINT8(0, r->dest.prefix);
  r = table_lookup(table, IP(192, 168, 44, 1));
  TEST_ASSERT_EQUAL_UINT8(16, r->dest.prefix);
  TEST_ASSERT_EQUAL_STRING("eth1", r->dev);
  table_destroy(table);
}

void test_table_lookup_order_does_not_matter(void)
{
  /* The example table reversed, so the default route is seen last. */
  fwd_table_t *forward = table_of(EXAMPLE_TABLE);
  fwd_table_t *reverse = table_of("192.168.0.0/16 10.1.0.1 eth1\n"
                                  "10.1.2.128/25 10.1.2.1 eth2\n"
                                  "10.1.2.0/24 direct eth2\n"
                                  "10.1.0.0/16 direct eth1\n"
                                  "10.0.0.0/8 10.1.0.1 eth1\n"
                                  "132.178.227.0/25 direct eno1\n"
                                  "default 132.178.227.1 eno1\n");
  /* And shortest first, so a first-match lookup would always say /8. */
  fwd_table_t *shortest = table_of("10.0.0.0/8 10.1.0.1 eth1\n"
                                   "10.1.0.0/16 direct eth1\n"
                                   "10.1.2.0/24 direct eth2\n"
                                   "10.1.2.128/25 10.1.2.1 eth2\n");
  static const uint32_t probes[] = {
      IP(132, 178, 227, 50), IP(8, 8, 8, 8),   IP(10, 1, 2, 5),
      IP(10, 1, 2, 200),     IP(10, 1, 3, 1),  IP(10, 2, 0, 1),
      IP(192, 168, 44, 1),   IP(0, 0, 0, 0),   IP(255, 255, 255, 255),
      IP(10, 1, 2, 128),     IP(10, 1, 2, 127)};
  for (size_t k = 0; k < sizeof probes / sizeof probes[0]; k++) {
    const route_t *a = table_lookup(forward, probes[k]);
    const route_t *b = table_lookup(reverse, probes[k]);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_HEX32(a->dest.addr, b->dest.addr);
    TEST_ASSERT_EQUAL_UINT8(a->dest.prefix, b->dest.prefix);
    TEST_ASSERT_EQUAL_HEX32(a->gateway, b->gateway);
    TEST_ASSERT_EQUAL_STRING(a->dev, b->dev);
  }
  TEST_ASSERT_EQUAL_UINT8(25,
                          table_lookup(shortest, IP(10, 1, 2, 200))->dest.prefix);
  TEST_ASSERT_EQUAL_UINT8(24, table_lookup(shortest, IP(10, 1, 2, 5))->dest.prefix);
  table_destroy(forward);
  table_destroy(reverse);
  table_destroy(shortest);
}

void test_table_lookup_no_default_route(void)
{
  fwd_table_t *table = table_of("10.0.0.0/8 10.1.0.1 eth1\n"
                                "10.1.2.0/24 direct eth2\n");
  TEST_ASSERT_NULL(table_lookup(table, IP(8, 8, 8, 8)));
  TEST_ASSERT_NULL(table_lookup(table, IP(11, 0, 0, 0)));
  TEST_ASSERT_NULL(table_lookup(table, IP(9, 255, 255, 255)));
  TEST_ASSERT_EQUAL_STRING("eth1", dev_for(table, "10.0.0.0"));
  TEST_ASSERT_EQUAL_STRING("eth1", dev_for(table, "10.255.255.255"));
  table_destroy(table);
}

void test_table_lookup_boundaries(void)
{
  fwd_table_t *table = table_of("default direct eth0\n"
                                "10.0.0.7/32 direct host\n"
                                "10.0.0.6/31 direct p2p\n"
                                "255.255.255.255/32 direct top\n");
  TEST_ASSERT_EQUAL_STRING("host", dev_for(table, "10.0.0.7"));
  TEST_ASSERT_EQUAL_STRING("p2p", dev_for(table, "10.0.0.6"));
  TEST_ASSERT_EQUAL_STRING("eth0", dev_for(table, "10.0.0.8"));
  TEST_ASSERT_EQUAL_STRING("eth0", dev_for(table, "0.0.0.0"));
  TEST_ASSERT_EQUAL_STRING("top", dev_for(table, "255.255.255.255"));
  TEST_ASSERT_EQUAL_STRING("eth0", dev_for(table, "255.255.255.254"));
  table_destroy(table);
}

void test_table_lookup_only_default(void)
{
  /* With cidr_mask(0) wrong, the default route matches only 0.0.0.0. */
  fwd_table_t *table = table_of("default 192.0.2.1 eth0\n");
  TEST_ASSERT_EQUAL_STRING("eth0", dev_for(table, "8.8.8.8"));
  TEST_ASSERT_EQUAL_STRING("eth0", dev_for(table, "0.0.0.0"));
  TEST_ASSERT_EQUAL_STRING("eth0", dev_for(table, "255.255.255.255"));
  table_destroy(table);
}

void test_table_lookup_null(void)
{
  TEST_ASSERT_NULL(table_lookup(NULL, IP(8, 8, 8, 8)));
}

void test_table_load_example_file(void)
{
  /* The copy in the repository is the table the grading uses. */
  FILE *f = fopen("tables/example.txt", "r");
  fwd_table_t *table = table_create();
  size_t bad = SENTINEL;
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "run the tests from the repository root");
  TEST_ASSERT_TRUE(table_load(table, f, &bad));
  TEST_ASSERT_EQUAL_size_t(0, bad);
  TEST_ASSERT_EQUAL_size_t(7, table_count(table));
  TEST_ASSERT_EQUAL_STRING("eth2", dev_for(table, "10.1.2.200"));
  fclose(f);
  table_destroy(table);
}

void test_table_load_comments_and_blanks(void)
{
  fwd_table_t *table = table_of("\n"
                                "# only a comment\n"
                                "   # an indented comment\n"
                                "\t\n"
                                "10.0.0.0/8 direct eth1 # trailing comment\n"
                                "#10.1.0.0/16 direct eth9\n"
                                "10.1.0.0/16 direct eth2#no space\n"
                                "\n");
  TEST_ASSERT_EQUAL_size_t(2, table_count(table));
  TEST_ASSERT_EQUAL_STRING("eth2", dev_for(table, "10.1.0.1"));
  table_destroy(table);
}

void test_table_load_empty_file(void)
{
  fwd_table_t *table = table_of("");
  TEST_ASSERT_EQUAL_size_t(0, table_count(table));
  table_destroy(table);
}

void test_table_load_no_final_newline(void)
{
  fwd_table_t *table = table_of("10.0.0.0/8 direct eth1\n"
                                "default direct eth0");
  TEST_ASSERT_EQUAL_size_t(2, table_count(table));
  table_destroy(table);
}

void test_table_load_bad_line_in_middle(void)
{
  /* Line 4 is bad; the three routes before it stay, nothing after loads. */
  fwd_table_t *table = table_create();
  FILE *f = file_of("default 132.178.227.1 eno1\n"
                    "10.0.0.0/8 10.1.0.1 eth1\n"
                    "10.1.0.0/16 direct eth1\n"
                    "10.1.2.5/24 direct eth2\n"
                    "192.168.0.0/16 10.1.0.1 eth1\n");
  size_t bad = SENTINEL;
  TEST_ASSERT_FALSE(table_load(table, f, &bad));
  TEST_ASSERT_EQUAL_size_t(4, bad);
  TEST_ASSERT_EQUAL_size_t(3, table_count(table));
  TEST_ASSERT_EQUAL_STRING("eth1", dev_for(table, "10.1.2.5"));
  TEST_ASSERT_EQUAL_STRING("eno1", dev_for(table, "192.168.1.1"));
  fclose(f);
  table_destroy(table);
}

void test_table_load_bad_line_counts_comments(void)
{
  /* Line numbers count every line in the file, so the message points at it. */
  fwd_table_t *table = table_create();
  FILE *f = file_of("# header\n"
                    "\n"
                    "10.0.0.0/8 direct eth1\n"
                    "   # indented\n"
                    "not a route\n");
  size_t bad = SENTINEL;
  TEST_ASSERT_FALSE(table_load(table, f, &bad));
  TEST_ASSERT_EQUAL_size_t(5, bad);
  TEST_ASSERT_EQUAL_size_t(1, table_count(table));
  fclose(f);
  table_destroy(table);
}

void test_table_load_bad_lines_of_each_kind(void)
{
  static const char *bad[] = {
      "default direct eth0\n10.0.0.0/8 direct\n",
      "default direct eth0\n10.0.0.0/8 direct eth1 extra\n",
      "default direct eth0\n10.0.0.1/8 direct eth1\n",
      "default direct eth0\n010.0.0.0/8 direct eth1\n",
      /* A duplicate is a bad line too: table_add() refuses it. */
      "default direct eth0\ndefault 10.0.0.1 eth1\n",
  };
  for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
    fwd_table_t *table = table_create();
    FILE *f = file_of(bad[k]);
    size_t line = SENTINEL;
    TEST_ASSERT_FALSE_MESSAGE(table_load(table, f, &line), bad[k]);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, line, bad[k]);
    TEST_ASSERT_EQUAL_size_t(1, table_count(table));
    fclose(f);
    table_destroy(table);
  }
}

void test_table_load_bad_first_line_without_bad_line(void)
{
  /* bad_line may be NULL. */
  fwd_table_t *table = table_create();
  FILE *f = file_of("nonsense\n");
  TEST_ASSERT_FALSE(table_load(table, f, NULL));
  TEST_ASSERT_EQUAL_size_t(0, table_count(table));
  fclose(f);
  table_destroy(table);

  table = table_create();
  f = file_of("default direct eth0\n");
  TEST_ASSERT_TRUE(table_load(table, f, NULL));
  TEST_ASSERT_EQUAL_size_t(1, table_count(table));
  fclose(f);
  table_destroy(table);
}

void test_table_load_into_existing_table(void)
{
  /* Routes are added to what is already there, and duplicates still count. */
  fwd_table_t *table = table_of("10.0.0.0/8 direct eth1\n");
  FILE *f = file_of("default direct eth0\n10.0.0.0/8 direct eth1\n");
  size_t bad = SENTINEL;
  TEST_ASSERT_FALSE(table_load(table, f, &bad));
  TEST_ASSERT_EQUAL_size_t(2, bad);
  TEST_ASSERT_EQUAL_size_t(2, table_count(table));
  fclose(f);
  table_destroy(table);
}

void test_table_load_null(void)
{
  fwd_table_t *table = table_create();
  FILE *f = file_of("default direct eth0\n");
  size_t bad = SENTINEL;
  TEST_ASSERT_FALSE(table_load(NULL, f, &bad));
  TEST_ASSERT_EQUAL_size_t(0, bad);
  bad = SENTINEL;
  TEST_ASSERT_FALSE(table_load(table, NULL, &bad));
  TEST_ASSERT_EQUAL_size_t(0, bad);
  TEST_ASSERT_FALSE(table_load(NULL, NULL, NULL));
  TEST_ASSERT_EQUAL_size_t(0, table_count(table));
  fclose(f);
  table_destroy(table);
}

void test_table_load_read_error(void)
{
  /* A file open only for writing can not be read: getline() fails. */
  fwd_table_t *table = table_create();
  FILE *f = fopen("/dev/null", "w");
  size_t bad = SENTINEL;
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_FALSE(table_load(table, f, &bad));
  TEST_ASSERT_EQUAL_size_t(0, bad);
  TEST_ASSERT_EQUAL_size_t(0, table_count(table));
  fclose(f);
  table_destroy(table);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_ip_parse_valid);
  RUN_TEST(test_ip_parse_boundaries);
  RUN_TEST(test_ip_parse_rejects_malformed);
  RUN_TEST(test_ip_parse_null);
  RUN_TEST(test_ip_format);
  RUN_TEST(test_ip_round_trip);
  RUN_TEST(test_cidr_parse_valid);
  RUN_TEST(test_cidr_parse_boundaries);
  RUN_TEST(test_cidr_parse_rejects_malformed);
  RUN_TEST(test_cidr_parse_null);
  RUN_TEST(test_cidr_format);
  RUN_TEST(test_cidr_format_boundaries);
  RUN_TEST(test_cidr_mask);
  RUN_TEST(test_cidr_mask_boundaries);
  RUN_TEST(test_cidr_mask_every_prefix);
  RUN_TEST(test_onyx_subnet);
  RUN_TEST(test_cidr_ranges_ordinary);
  RUN_TEST(test_cidr_ranges_slash0);
  RUN_TEST(test_cidr_ranges_slash31);
  RUN_TEST(test_cidr_ranges_slash32);
  RUN_TEST(test_cidr_ranges_extreme_addresses);
  RUN_TEST(test_cidr_host_count_every_prefix);
  RUN_TEST(test_cidr_prefix_over_32_is_32);
  RUN_TEST(test_cidr_contains);
  RUN_TEST(test_cidr_contains_boundaries);
  RUN_TEST(test_cidr_is_network);
  RUN_TEST(test_cidr_subnet_handout_example);
  RUN_TEST(test_cidr_subnet_one_is_the_block);
  RUN_TEST(test_cidr_subnet_boundaries);
  RUN_TEST(test_cidr_subnet_covers_block_in_order);
  RUN_TEST(test_cidr_subnet_rejects);
  RUN_TEST(test_cidr_subnet_prefix_over_32);
  RUN_TEST(test_route_parse_gateway);
  RUN_TEST(test_route_parse_default_and_direct);
  RUN_TEST(test_route_parse_whitespace);
  RUN_TEST(test_route_parse_device_length);
  RUN_TEST(test_route_parse_rejects);
  RUN_TEST(test_route_parse_null);
  RUN_TEST(test_table_create_destroy);
  RUN_TEST(test_table_count_null);
  RUN_TEST(test_table_add);
  RUN_TEST(test_table_add_rejects);
  RUN_TEST(test_table_add_two_defaults);
  RUN_TEST(test_table_add_same_network_other_prefix);
  RUN_TEST(test_table_add_grows);
  RUN_TEST(test_table_lookup_longest_prefix);
  RUN_TEST(test_table_lookup_order_does_not_matter);
  RUN_TEST(test_table_lookup_no_default_route);
  RUN_TEST(test_table_lookup_boundaries);
  RUN_TEST(test_table_lookup_only_default);
  RUN_TEST(test_table_lookup_null);
  RUN_TEST(test_table_load_example_file);
  RUN_TEST(test_table_load_comments_and_blanks);
  RUN_TEST(test_table_load_empty_file);
  RUN_TEST(test_table_load_no_final_newline);
  RUN_TEST(test_table_load_bad_line_in_middle);
  RUN_TEST(test_table_load_bad_line_counts_comments);
  RUN_TEST(test_table_load_bad_lines_of_each_kind);
  RUN_TEST(test_table_load_bad_first_line_without_bad_line);
  RUN_TEST(test_table_load_into_existing_table);
  RUN_TEST(test_table_load_null);
  RUN_TEST(test_table_load_read_error);
  return UNITY_END();
}
