#ifndef LAB_H
#define LAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Every address in this file is a uint32_t in host byte order, so the first
 * octet of 192.168.1.10 is the most significant byte: 0xC0A8010A.
 */

/** Room for the longest dotted quad, "255.255.255.255", and its '\0'. */
#define IP_STR_LEN 16

/** Room for the longest CIDR string, "255.255.255.255/32", and its '\0'. */
#define CIDR_STR_LEN 19

/** Room for an interface name; IFNAMSIZ on Linux, including the '\0'. */
#define DEV_NAME_LEN 16

/** An address block in CIDR notation: an address and a prefix length. */
typedef struct {
  uint32_t addr;  /**< any address in the block, host byte order */
  uint8_t prefix; /**< 0 to 32 */
} cidr_t;

/** One entry in a forwarding table. */
typedef struct {
  cidr_t dest;            /**< destination block; host bits must be zero */
  uint32_t gateway;       /**< next hop, or 0 when the block is directly connected */
  char dev[DEV_NAME_LEN]; /**< outgoing interface, for example "eth0" */
} route_t;

/** A forwarding table. Its layout is private to lab.c. */
typedef struct fwd_table fwd_table_t;

/* ------------------------------------------------------------------------
 * Task 1 - Address parsing and formatting
 * --------------------------------------------------------------------- */

/**
 * @brief Parse a dotted quad such as "192.168.1.10".
 *
 * Exactly four decimal octets from 0 to 255 separated by single periods, with
 * nothing before, between or after them. An octet with a leading zero ("010")
 * is rejected, because inet_aton() reads it as octal and that is never what a
 * person typing it meant.
 *
 * @param text The string to parse.
 * @param addr Where the address is stored on success; untouched on failure.
 * @return true if text is a valid dotted quad, false otherwise (including NULL).
 */
bool ip_parse(const char *text, uint32_t *addr);

/**
 * @brief Format an address as a dotted quad.
 * @param addr The address, host byte order.
 * @param out A buffer of at least IP_STR_LEN bytes.
 */
void ip_format(uint32_t addr, char out[IP_STR_LEN]);

/**
 * @brief Parse a block in CIDR notation such as "192.168.1.0/24".
 *
 * The address follows the rules of ip_parse() and the prefix is a decimal
 * number from 0 to 32 with no leading zero. The slash and the prefix are
 * required. Host bits may be set ("132.178.227.11/25" is an interface
 * address), so use cidr_is_network() when they must not be.
 *
 * @param text The string to parse.
 * @param block Where the block is stored on success; untouched on failure.
 * @return true if text is valid CIDR notation, false otherwise (including NULL).
 */
bool cidr_parse(const char *text, cidr_t *block);

/**
 * @brief Format a block as "a.b.c.d/p", using the address exactly as stored.
 *
 * A prefix over 32 is written as 32, the way cidr_mask() treats it.
 *
 * @param block The block.
 * @param out A buffer of at least CIDR_STR_LEN bytes.
 */
void cidr_format(cidr_t block, char out[CIDR_STR_LEN]);

/* ------------------------------------------------------------------------
 * Task 2 - Subnet arithmetic
 * --------------------------------------------------------------------- */

/**
 * @brief The subnet mask for a prefix length: /24 is 255.255.255.0.
 *
 * Careful with /0. Shifting a 32 bit value by 32 is undefined behavior in C.
 *
 * @param prefix 0 to 32; anything larger is treated as 32.
 * @return The mask, host byte order.
 */
uint32_t cidr_mask(uint8_t prefix);

/** @brief The lowest address in the block: the address with the host bits cleared. */
uint32_t cidr_network(cidr_t block);

/** @brief The highest address in the block: the address with the host bits set. */
uint32_t cidr_broadcast(cidr_t block);

/**
 * @brief The first address that can be given to a host.
 *
 * For /0 to /30 that is the network address plus one. A /31 is a point to
 * point link (RFC 3021) where both addresses are usable, and a /32 is a single
 * host, so for those two the first host is the network address.
 */
uint32_t cidr_first_host(cidr_t block);

/**
 * @brief The last address that can be given to a host.
 *
 * The broadcast address minus one for /0 to /30, and the broadcast address
 * itself for /31 and /32 (see cidr_first_host()).
 */
uint32_t cidr_last_host(cidr_t block);

/**
 * @brief How many addresses in the block can be given to hosts.
 *
 * 2^(32 - prefix) - 2 for /0 to /30, 2 for /31, and 1 for /32. For a /0 the
 * 2^32 in that formula needs 33 bits, more than a uint32_t holds, which is why
 * this returns a uint64_t.
 */
uint64_t cidr_host_count(cidr_t block);

/** @brief true if addr falls inside the block. */
bool cidr_contains(cidr_t block, uint32_t addr);

/** @brief true if the block's host bits are all zero, so it names a network. */
bool cidr_is_network(cidr_t block);

/**
 * @brief The i-th of n equally sized subnets of a block.
 *
 * Splitting a /24 into 4 gives four /26 blocks, and subnet 0 is the lowest.
 * Equal sizes are only possible when n is a power of two, and the new prefix
 * can not pass 32.
 *
 * @param block The block to split; it must be a network (cidr_is_network()).
 * @param n How many subnets, a power of two from 1 up.
 * @param i Which subnet, 0 to n - 1.
 * @param out Where the subnet is stored on success; untouched on failure.
 * @return false if out is NULL, the block is not a network, n is not a power
 *         of two, the split would need a prefix longer than 32, or i is out of
 *         range.
 */
bool cidr_subnet(cidr_t block, uint32_t n, uint32_t i, cidr_t *out);

/* ------------------------------------------------------------------------
 * Task 3 - Longest prefix match
 * --------------------------------------------------------------------- */

/**
 * @brief Parse one line of a forwarding table file.
 *
 * A line is three fields separated by spaces or tabs:
 *
 *     <destination> <gateway> <device>
 *
 * The destination is a block in CIDR notation with no host bits set, or the
 * word "default" for 0.0.0.0/0. The gateway is a dotted quad, or the word
 * "direct" for a directly connected block (stored as 0). The device is 1 to
 * DEV_NAME_LEN - 1 characters. Leading and trailing whitespace, including a
 * trailing newline, is ignored. Comments are not handled here.
 *
 * @param line The line.
 * @param route Where the route is stored on success; untouched on failure.
 * @return true if the line is a valid route, false otherwise (including NULL).
 */
bool route_parse(const char *line, route_t *route);

/**
 * @brief Create an empty forwarding table.
 * @return The table, which the caller frees with table_destroy(), or NULL if
 *         memory could not be allocated.
 */
fwd_table_t *table_create(void);

/** @brief Free a table and everything in it. NULL is allowed and does nothing. */
void table_destroy(fwd_table_t *table);

/** @brief The number of routes in the table; 0 for NULL. */
size_t table_count(const fwd_table_t *table);

/**
 * @brief Add a copy of a route to the table, growing it as needed.
 * @return false if either argument is NULL, the destination has host bits
 *         set, the table already holds a route to the same block, or memory
 *         could not be allocated. The table is unchanged on failure.
 */
bool table_add(fwd_table_t *table, const route_t *route);

/**
 * @brief Read a forwarding table file and add every route in it.
 *
 * Everything from a '#' to the end of a line is a comment, and a line that is
 * blank once the comment is removed is skipped. Every other line must be a
 * valid route (route_parse()) that table_add() accepts.
 *
 * @param table The table to add to.
 * @param in The open file to read.
 * @param bad_line If not NULL, set to the 1-based number of the first line
 *        that failed, or 0 if the arguments were NULL or the file could not
 *        be read.
 * @return true if every line loaded. On false, routes from the lines before
 *         the bad one stay in the table.
 */
bool table_load(fwd_table_t *table, FILE *in, size_t *bad_line);

/**
 * @brief Longest prefix match: the route whose block contains dest and has the
 *        longest prefix.
 *
 * A default route (0.0.0.0/0) matches everything, so it is chosen only when
 * nothing more specific does. The order the routes were added in does not
 * matter.
 *
 * @return The matching route, which stays owned by the table, or NULL if no
 *         route matches (or table is NULL).
 */
const route_t *table_lookup(const fwd_table_t *table, uint32_t dest);

#endif // LAB_H
