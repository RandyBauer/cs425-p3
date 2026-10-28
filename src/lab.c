#include "lab.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * The forwarding table is a growable array of routes. Lookup is a linear scan
 * that remembers the longest matching prefix seen so far, so the order the
 * routes were added in never changes the answer.
 */
struct fwd_table {
  route_t *routes; /* count used, capacity allocated */
  size_t count;
  size_t capacity;
};

/* Capacity of the first allocation; it doubles after that. */
#define TABLE_INITIAL_CAPACITY 8

/* A prefix over 32 is treated as 32 everywhere, as cidr_mask() documents. */
static uint32_t clamp_prefix(uint8_t prefix)
{
  return prefix > 32 ? 32u : prefix;
}

/*
 * Read a decimal number at *cursor and move *cursor past it. At least one
 * digit, no leading zero unless the number is exactly "0", and no larger than
 * max. Accumulation stops as soon as the value passes max, so a long run of
 * digits can not overflow.
 */
static bool parse_decimal(const char **cursor, uint32_t max, uint32_t *value)
{
  const char *p = *cursor;
  uint32_t result = 0;

  if (*p < '0' || *p > '9') {
    return false;
  }
  if (*p == '0' && p[1] >= '0' && p[1] <= '9') {
    return false;
  }
  while (*p >= '0' && *p <= '9') {
    result = result * 10u + (uint32_t)(*p - '0');
    if (result > max) {
      return false;
    }
    p++;
  }
  *cursor = p;
  *value = result;
  return true;
}

/*
 * Read a dotted quad at *cursor and move *cursor past it. Whatever follows
 * the fourth octet is left for the caller to judge.
 */
static bool parse_quad(const char **cursor, uint32_t *addr)
{
  const char *p = *cursor;
  uint32_t result = 0;

  for (int i = 0; i < 4; i++) {
    uint32_t octet;
    if (i > 0) {
      if (*p != '.') {
        return false;
      }
      p++;
    }
    if (!parse_decimal(&p, 255, &octet)) {
      return false;
    }
    result = result << 8 | octet;
  }
  *cursor = p;
  *addr = result;
  return true;
}

bool ip_parse(const char *text, uint32_t *addr)
{
  uint32_t result;

  if (text == NULL || addr == NULL) {
    return false;
  }
  if (!parse_quad(&text, &result) || *text != '\0') {
    return false;
  }
  *addr = result;
  return true;
}

void ip_format(uint32_t addr, char out[IP_STR_LEN])
{
  snprintf(out, IP_STR_LEN, "%u.%u.%u.%u", addr >> 24, addr >> 16 & 0xFFu,
           addr >> 8 & 0xFFu, addr & 0xFFu);
}

bool cidr_parse(const char *text, cidr_t *block)
{
  uint32_t addr;
  uint32_t prefix;

  if (text == NULL || block == NULL) {
    return false;
  }
  if (!parse_quad(&text, &addr) || *text != '/') {
    return false;
  }
  text++;
  if (!parse_decimal(&text, 32, &prefix) || *text != '\0') {
    return false;
  }
  block->addr = addr;
  block->prefix = (uint8_t)prefix;
  return true;
}

void cidr_format(cidr_t block, char out[CIDR_STR_LEN])
{
  uint32_t a = block.addr;

  snprintf(out, CIDR_STR_LEN, "%u.%u.%u.%u/%u", a >> 24, a >> 16 & 0xFFu,
           a >> 8 & 0xFFu, a & 0xFFu, clamp_prefix(block.prefix));
}

uint32_t cidr_mask(uint8_t prefix)
{
  uint32_t p = clamp_prefix(prefix);

  /* Shifting a 32 bit value by 32 is undefined, so /0 never reaches the shift. */
  if (p == 0) {
    return 0;
  }
  return UINT32_MAX << (32u - p);
}

uint32_t cidr_network(cidr_t block)
{
  return block.addr & cidr_mask(block.prefix);
}

uint32_t cidr_broadcast(cidr_t block)
{
  return block.addr | ~cidr_mask(block.prefix);
}

uint32_t cidr_first_host(cidr_t block)
{
  /* /31 (RFC 3021) and /32 have no network address to skip. */
  if (clamp_prefix(block.prefix) >= 31) {
    return cidr_network(block);
  }
  return cidr_network(block) + 1u;
}

uint32_t cidr_last_host(cidr_t block)
{
  /* /31 (RFC 3021) and /32 have no broadcast address to skip. */
  if (clamp_prefix(block.prefix) >= 31) {
    return cidr_broadcast(block);
  }
  return cidr_broadcast(block) - 1u;
}

uint64_t cidr_host_count(cidr_t block)
{
  uint32_t p = clamp_prefix(block.prefix);

  if (p == 32) {
    return 1;
  }
  if (p == 31) {
    return 2;
  }
  /* 64 bits, because 2^32 for a /0 does not fit in a uint32_t. */
  return (UINT64_C(1) << (32u - p)) - 2u;
}

bool cidr_contains(cidr_t block, uint32_t addr)
{
  uint32_t mask = cidr_mask(block.prefix);

  return (addr & mask) == (block.addr & mask);
}

bool cidr_is_network(cidr_t block)
{
  return (block.addr & ~cidr_mask(block.prefix)) == 0;
}

bool cidr_subnet(cidr_t block, uint32_t n, uint32_t i, cidr_t *out)
{
  uint32_t borrowed = 0;
  uint32_t prefix;

  if (out == NULL || !cidr_is_network(block)) {
    return false;
  }
  if (n == 0 || (n & (n - 1u)) != 0) {
    return false;
  }
  while ((UINT32_C(1) << borrowed) < n) {
    borrowed++;
  }
  prefix = clamp_prefix(block.prefix) + borrowed;
  if (prefix > 32 || i >= n) {
    return false;
  }
  /*
   * Subnet i starts i blocks of 2^(32 - prefix) above the network. The shift
   * is done in 64 bits because splitting a /0 into 1 shifts by 32. The result
   * always fits: i < 2^borrowed, so the offset is below 2^(32 - old prefix).
   */
  out->addr = cidr_network(block) + (uint32_t)((uint64_t)i << (32u - prefix));
  out->prefix = (uint8_t)prefix;
  return true;
}

/*
 * Copy the next field, a run of characters other than space and tab, from
 * [*cursor, end) into buf, skipping the spaces and tabs before it. Fails when
 * there is no field or it does not fit in size - 1 characters.
 */
static bool next_field(const char **cursor, const char *end, char *buf,
                       size_t size)
{
  const char *p = *cursor;
  const char *start;
  size_t len;

  while (p < end && (*p == ' ' || *p == '\t')) {
    p++;
  }
  start = p;
  while (p < end && *p != ' ' && *p != '\t') {
    p++;
  }
  len = (size_t)(p - start);
  if (len == 0 || len >= size) {
    return false;
  }
  memcpy(buf, start, len);
  buf[len] = '\0';
  *cursor = p;
  return true;
}

bool route_parse(const char *line, route_t *route)
{
  char dest_text[CIDR_STR_LEN];
  char gateway_text[IP_STR_LEN];
  char dev[DEV_NAME_LEN];
  route_t result;
  const char *end;

  if (line == NULL || route == NULL) {
    return false;
  }
  while (isspace((unsigned char)*line)) {
    line++;
  }
  end = line + strlen(line);
  while (end > line && isspace((unsigned char)end[-1])) {
    end--;
  }

  if (!next_field(&line, end, dest_text, sizeof dest_text) ||
      !next_field(&line, end, gateway_text, sizeof gateway_text) ||
      !next_field(&line, end, dev, sizeof dev)) {
    return false;
  }
  /* Only spaces and tabs separate fields, so anything left is a fourth field. */
  if (line != end) {
    return false;
  }

  if (strcmp(dest_text, "default") == 0) {
    result.dest.addr = 0;
    result.dest.prefix = 0;
  } else if (!cidr_parse(dest_text, &result.dest) ||
             !cidr_is_network(result.dest)) {
    return false;
  }

  if (strcmp(gateway_text, "direct") == 0) {
    result.gateway = 0;
  } else if (!ip_parse(gateway_text, &result.gateway)) {
    return false;
  }

  /* A newline or carriage return inside the line is not part of a name. */
  for (const char *c = dev; *c != '\0'; c++) {
    if (isspace((unsigned char)*c)) {
      return false;
    }
  }
  memcpy(result.dev, dev, sizeof dev);

  *route = result;
  return true;
}

fwd_table_t *table_create(void)
{
  /* calloc leaves routes NULL and both counts 0; the first add allocates. */
  return calloc(1, sizeof(fwd_table_t));
}

void table_destroy(fwd_table_t *table)
{
  if (table == NULL) {
    return;
  }
  free(table->routes);
  free(table);
}

size_t table_count(const fwd_table_t *table)
{
  return table == NULL ? 0 : table->count;
}

bool table_add(fwd_table_t *table, const route_t *route)
{
  if (table == NULL || route == NULL || !cidr_is_network(route->dest)) {
    return false;
  }

  /* The same block means the same network and the same prefix length. */
  for (size_t k = 0; k < table->count; k++) {
    const cidr_t *have = &table->routes[k].dest;
    if (clamp_prefix(have->prefix) == clamp_prefix(route->dest.prefix) &&
        have->addr == route->dest.addr) {
      return false;
    }
  }

  if (table->count == table->capacity) {
    size_t capacity =
        table->capacity == 0 ? TABLE_INITIAL_CAPACITY : table->capacity * 2;
    route_t *grown = realloc(table->routes, capacity * sizeof(route_t));
    if (grown == NULL) { // GCOVR_EXCL_START
      return false;      /* the old array is still there, so nothing changed */
    }                    // GCOVR_EXCL_STOP
    table->routes = grown;
    table->capacity = capacity;
  }

  table->routes[table->count] = *route;
  table->count++;
  return true;
}

/* true if the line holds nothing but whitespace. */
static bool is_blank(const char *line)
{
  while (isspace((unsigned char)*line)) {
    line++;
  }
  return *line == '\0';
}

bool table_load(fwd_table_t *table, FILE *in, size_t *bad_line)
{
  char *line = NULL;
  size_t size = 0;
  size_t number = 0;
  bool ok = true;

  if (bad_line != NULL) {
    *bad_line = 0;
  }
  if (table == NULL || in == NULL) {
    return false;
  }

  while (getline(&line, &size, in) >= 0) {
    char *comment;
    route_t route;

    number++;
    comment = strchr(line, '#');
    if (comment != NULL) {
      *comment = '\0';
    }
    if (is_blank(line)) {
      continue;
    }
    if (!route_parse(line, &route) || !table_add(table, &route)) {
      if (bad_line != NULL) {
        *bad_line = number;
      }
      ok = false;
      break;
    }
  }

  /* getline() returns -1 for a read error as well as for the end of the file. */
  if (ok && ferror(in)) {
    ok = false;
  }
  free(line);
  return ok;
}

const route_t *table_lookup(const fwd_table_t *table, uint32_t dest)
{
  const route_t *best = NULL;

  if (table == NULL) {
    return NULL;
  }
  /* Keep the longest match seen so far; a shorter one never replaces it. */
  for (size_t k = 0; k < table->count; k++) {
    const route_t *r = &table->routes[k];
    if (cidr_contains(r->dest, dest) &&
        (best == NULL ||
         clamp_prefix(r->dest.prefix) > clamp_prefix(best->dest.prefix))) {
      best = r;
    }
  }
  return best;
}
