#include "lab.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST
#define main main_exclude
#endif

/* Exit codes, from the handout. */
#define EXIT_USAGE 1       /* bad command line or malformed address */
#define EXIT_TABLE 2       /* table file can not be opened or has a bad line */
#define EXIT_UNREACHABLE 3 /* an address matches no route */

static void usage(FILE *out)
{
  fprintf(out, "Usage: myapp info <address>/<prefix>\n"
               "       myapp split <network>/<prefix> <count>\n"
               "       myapp lookup <table-file> <address>...\n");
}

/*
 * Parse a subnet count: decimal digits only, so "-4", "+4", " 4" and "4x" are
 * all refused, and the value must fit in a uint32_t.
 */
static bool parse_count(const char *text, uint32_t *count)
{
  char *end;
  unsigned long value;

  if (text[0] < '0' || text[0] > '9') {
    return false;
  }
  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno != 0 || *end != '\0' || value > UINT32_MAX) {
    return false;
  }
  *count = (uint32_t)value;
  return true;
}

/* myapp info <address>/<prefix> */
static int run_info(const char *text)
{
  cidr_t block;
  char buf[IP_STR_LEN];

  if (!cidr_parse(text, &block)) {
    fprintf(stderr, "myapp: '%s' is not a valid <address>/<prefix>\n", text);
    return EXIT_USAGE;
  }
  ip_format(block.addr, buf);
  printf("%-11s%s\n", "address", buf);
  printf("%-11s/%u\n", "prefix", (unsigned)block.prefix);
  ip_format(cidr_mask(block.prefix), buf);
  printf("%-11s%s\n", "netmask", buf);
  ip_format(cidr_network(block), buf);
  printf("%-11s%s\n", "network", buf);
  ip_format(cidr_broadcast(block), buf);
  printf("%-11s%s\n", "broadcast", buf);
  ip_format(cidr_first_host(block), buf);
  printf("%-11s%s\n", "first host", buf);
  ip_format(cidr_last_host(block), buf);
  printf("%-11s%s\n", "last host", buf);
  printf("%-11s%" PRIu64 "\n", "hosts", cidr_host_count(block));
  return EXIT_SUCCESS;
}

/* myapp split <network>/<prefix> <count> */
static int run_split(const char *text, const char *count_text)
{
  cidr_t block;
  cidr_t subnet;
  uint32_t count;
  char buf[CIDR_STR_LEN];

  if (!cidr_parse(text, &block)) {
    fprintf(stderr, "myapp: '%s' is not a valid <network>/<prefix>\n", text);
    return EXIT_USAGE;
  }
  if (!cidr_is_network(block)) {
    cidr_t network = {cidr_network(block), block.prefix};
    cidr_format(network, buf);
    fprintf(stderr, "myapp: %s has host bits set; the network is %s\n", text,
            buf);
    return EXIT_USAGE;
  }
  if (!parse_count(count_text, &count) || count == 0 ||
      (count & (count - 1u)) != 0) {
    fprintf(stderr,
            "myapp: count '%s' must be a power of two from 1 to 2147483648\n",
            count_text);
    return EXIT_USAGE;
  }
  if (!cidr_subnet(block, count, 0, &subnet)) {
    fprintf(stderr,
            "myapp: %s can not be split into %s: the prefix would pass /32\n",
            text, count_text);
    return EXIT_USAGE;
  }
  for (uint32_t i = 0; i < count; i++) {
    cidr_subnet(block, count, i, &subnet);
    cidr_format(subnet, buf);
    printf("%s\n", buf);
  }
  return EXIT_SUCCESS;
}

/* myapp lookup <table-file> <address>... */
static int run_lookup(const char *path, int naddr, char *addrs[])
{
  fwd_table_t *table;
  FILE *in;
  size_t bad_line;
  bool loaded;
  bool malformed = false;
  bool unreachable = false;

  in = fopen(path, "r");
  if (in == NULL) {
    fprintf(stderr, "myapp: %s: %s\n", path, strerror(errno));
    return EXIT_TABLE;
  }
  table = table_create();
  if (table == NULL) {
    fprintf(stderr, "myapp: out of memory\n");
    fclose(in);
    return EXIT_TABLE;
  }
  loaded = table_load(table, in, &bad_line);
  fclose(in);
  if (!loaded) {
    if (bad_line > 0) {
      fprintf(stderr, "myapp: %s:%zu: not a valid route\n", path, bad_line);
    } else {
      fprintf(stderr, "myapp: %s: could not be read\n", path);
    }
    table_destroy(table);
    return EXIT_TABLE;
  }

  /* Every address is reported, even after one fails. */
  for (int k = 0; k < naddr; k++) {
    uint32_t dest;
    const route_t *route;
    char dest_buf[IP_STR_LEN];
    char gateway_buf[IP_STR_LEN];

    if (!ip_parse(addrs[k], &dest)) {
      fprintf(stderr, "myapp: '%s' is not a valid address\n", addrs[k]);
      malformed = true;
      continue;
    }
    ip_format(dest, dest_buf);
    route = table_lookup(table, dest);
    if (route == NULL) {
      printf("%s unreachable\n", dest_buf);
      unreachable = true;
    } else if (route->gateway == 0) {
      printf("%s dev %s\n", dest_buf, route->dev);
    } else {
      ip_format(route->gateway, gateway_buf);
      printf("%s via %s dev %s\n", dest_buf, gateway_buf, route->dev);
    }
  }

  table_destroy(table);
  if (malformed) {
    return EXIT_USAGE;
  }
  return unreachable ? EXIT_UNREACHABLE : EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
  if (argc == 1) {
    usage(stdout);
    return EXIT_SUCCESS;
  }
  if (strcmp(argv[1], "info") == 0) {
    if (argc == 3) {
      return run_info(argv[2]);
    }
    fprintf(stderr, "myapp: info takes one <address>/<prefix>\n");
  } else if (strcmp(argv[1], "split") == 0) {
    if (argc == 4) {
      return run_split(argv[2], argv[3]);
    }
    fprintf(stderr, "myapp: split takes a <network>/<prefix> and a <count>\n");
  } else if (strcmp(argv[1], "lookup") == 0) {
    if (argc >= 4) {
      return run_lookup(argv[2], argc - 3, argv + 3);
    }
    fprintf(stderr, "myapp: lookup takes a <table-file> and at least one "
                    "<address>\n");
  } else {
    fprintf(stderr, "myapp: unknown command '%s'\n", argv[1]);
  }
  usage(stderr);
  return EXIT_USAGE;
}
