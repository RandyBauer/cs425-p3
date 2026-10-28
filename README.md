# Project 3 - Subnetting and Forwarding

- Name: Randy Bauer
- Email: randybauer@u.boisestate.edu
- Class: CS425-001

CIDR address arithmetic and a longest prefix match forwarding table, as a C library
(`src/lab.h`, `src/lab.c`) and a three-command program.

## Building and running

```bash
make all && make check
./build/release/myapp info 132.178.227.11/25
./build/release/myapp split 192.168.1.0/24 4
./build/release/myapp lookup tables/example.txt 132.178.227.50 8.8.8.8 10.1.2.5 10.1.2.200
```

| Exit code | Meaning |
| --- | --- |
| 0 | Success, or no arguments at all (prints usage) |
| 1 | Bad command line, malformed address or block, `split` of a block with host bits set, or a count that is not a power of two or is too large |
| 2 | The table file cannot be opened or read, or has a bad line |
| 3 | An address matched no route |

`lookup` reports every address before it exits. If any address was malformed the exit code is
1, otherwise 3 if any was unreachable. An unreachable address prints `<address> unreachable` on
stdout, in the same one-line-per-address form as the others. Errors go to stderr, for example
`myapp: tables/bad.txt:4: not a valid route`.

## Design

An address is a `uint32_t` in host byte order, and every value comes from the mask:

| Value | Computed as |
| --- | --- |
| mask | `UINT32_MAX << (32 - prefix)`, with `/0` handled first |
| network | `addr & mask` |
| broadcast | `addr \| ~mask` |
| contains `x` | `(x & mask) == (addr & mask)` |
| is a network | `(addr & ~mask) == 0` |

A prefix over 32 is treated as 32 in every function, through one helper, `clamp_prefix`.

### `/0`, `/31` and `/32`

- **`/0`.** The mask would be a shift by 32, which is undefined behavior in C. On x86 the shift
  count is reduced to its low 5 bits, so the shift is by 0 and the mask comes out as
  `0xFFFFFFFF`. Then the default route only matches `0.0.0.0`. `cidr_mask` returns 0 for `/0`
  before it gets to the shift. The host count of a `/0` is 2^32 - 2, and 2^32 needs 33 bits, so
  `cidr_host_count` does that arithmetic in `uint64_t`. Splitting a `/0` into 1 subnet also
  needs a shift by 32, so `cidr_subnet` does its offset shift in 64 bits.
- **`/31`.** A point-to-point link (RFC 3021) has no network or broadcast address. Both
  addresses are hosts: the first host is the network address, the last host is the broadcast
  address, and the host count is 2.
- **`/32`.** A single host. First host, last host, network and broadcast are all the address
  itself, and the host count is 1. The formula 2^0 - 2 would give -1, which is 4294967295 in an
  unsigned type.

`/31` and `/32` are checked before the formula in `cidr_first_host`, `cidr_last_host` and
`cidr_host_count`. The `+ 1` and `- 1` for the other prefixes therefore can not wrap at
`0.0.0.0` or `255.255.255.255`.

### Parsing

The parser is written by hand. It does not use `inet_aton`, `inet_pton` or `sscanf`. One helper
reads a decimal number with a maximum: at least one digit, no leading zero unless the number is
exactly `0`, and it stops as soon as the value passes the maximum, so a long run of digits can
not overflow. A dotted quad is four of those (maximum 255) with a `.` between them. CIDR is a
dotted quad, a `/`, and one more (maximum 32). Both parsers then require the end of the string.
They parse into local variables and write the output only on success.

`route_parse` trims leading and trailing whitespace (which includes `\r` from a file saved on
Windows), then splits on runs of spaces and tabs into exactly three fields. The destination is
`default` or CIDR with no host bits. The gateway is `direct` (stored as 0) or a dotted quad. The
device is 1 to 15 characters. A longer name is refused, not cut short.

### The forwarding table and the longest prefix

`struct fwd_table` is a growable array of `route_t`, with a count and a capacity. The first
`table_add` allocates 8 slots and every later growth doubles it. `realloc` goes into a temporary
pointer, so on failure the old array is still there and the table is unchanged. `table_add`
refuses a route with host bits set, and a second route to the same block. The same block means
the same network address and the same prefix length, so `10.0.0.0/8` and `10.0.0.0/16` are both
allowed, and so is only one `default`.

`table_lookup` scans every route. It keeps the longest matching prefix it has seen so far, and
replaces it only with a match whose prefix is strictly longer. It never stops at the first
match, so the order the routes were added in does not change the answer. Duplicate blocks are
refused, so two matches can not have the same prefix and there is no tie to break. The default
route is not a special case. `0.0.0.0/0` contains every address because its mask is 0, and any
other match has a longer prefix, so the default route is picked only when nothing more specific
matches. A linear scan costs one comparison per route. That is fine for a table this size; a
router with the full Internet table would use a binary trie instead.

`table_load` reads with `getline`. It cuts each line at the first `#`, skips lines that are
blank after that, and passes the rest to `route_parse` and `table_add`. Line numbers count every
line in the file, comments and blank lines included, so the number in an error message matches
the line in an editor. On a bad line it frees the `getline` buffer and returns false. The routes
from earlier lines stay in the table, and `table_destroy` frees them. A read error (`ferror`
after `getline` returns -1) also returns false, with `bad_line` 0.

### The program

`src/main.c` holds the three commands, because `src/lab.h` is fixed. `info` prints each label
padded to 11 columns. `split` checks the block and the count, then asks `cidr_subnet` for one
subnet at a time and prints it, so `split 0.0.0.0/0 2147483648` uses no extra memory. The count
must be all digits (so `-4`, `+4` and `4x` are refused) and fit in a `uint32_t`.

## Testing

`tests/lab-test.c` has 62 Unity tests covering every function in `src/lab.h`:

- the ten malformed strings from the handout, each one separately, and about thirty more
  (signs, spaces, empty octets, `00`, an octet with eleven digits, `/00`, `/+5`, `//5`), each
  checked to leave the output untouched;
- `/0`, `/31` and `/32` through every arithmetic function, `0.0.0.0` and `255.255.255.255` as
  addresses, every mask from `/0` to `/32`, and the host count of every prefix from `/0` to
  `/30`;
- `cidr_subnet` for the handout's example, `n = 1`, `/0` into 2 and into 2^31, and each of its
  five ways to fail (`n = 0` included);
- the handout's table, both from a string through `tmpfile()` and from `tables/example.txt`,
  with the handout's four lookups and four more;
- the same table loaded in reverse order, and a table sorted shortest first, where a first-match
  lookup would give the wrong answer;
- a table with no default route, a table with only a default route, and a bad line in the middle
  of a file (line 4, after three good routes);
- line numbers that count comments, a duplicate route as a bad line, an empty file, a missing
  final newline, and a read error.

`make report` gives 100% line coverage of `src/lab.c`. The only excluded lines are the
`realloc` failure branch. `make leak` and `make leak-test` report nothing.

The program was checked by a script that compares the three handout transcripts byte for byte
and checks 27 more commands for their exit codes. It was run against the release build and,
with `ASAN_OPTIONS=detect_leaks=1`, against the debug build.

## Known Bugs or Issues

No known bugs. The handout does not give the exact text of an unreachable line; this program
prints `<address> unreachable` on stdout. IPv4 only.

## AI Usage

All of `src/lab.c`, `src/main.c` and `tests/lab-test.c`, and this README, were written by an AI
assistant (Claude) at my request. `src/lab.h` and `tables/example.txt` are copied from the
handout. The assistant ran the builds, tests, coverage and leak checks in my WSL environment and
on Onyx.

## Experience

I did not write the code for this project. I asked an AI assistant (Claude) to write it and run
the checks, the same way I did P2, and to walk me through it afterward. P3 was due two days after
I started it, so I chose speed over writing it myself.

The part I understand best is the subnet math, because I already did it by hand for Onyx in A4.
An address is one 32-bit number, the mask keeps the network bits, and the broadcast address is
the network with every host bit turned on. The program prints the same numbers I worked out on
paper.

What happened along the way:

- The handout warns that the obvious mask for `/0` shifts by 32, which C does not define. The
  assistant checked it in WSL first. On my machine the bad version gives back all ones, so the
  default route would only match `0.0.0.0`. There is a test for it now.
- One error message was wrong. `split 192.168.1.0/24 4294967296` said the count was not a power
  of two, but 2^32 is one. It just does not fit in 32 bits. The message now gives the allowed
  range.
- The handout says splitting `0.0.0.0/0` into 2^31 pieces is a legal request. We ran it. It
  printed about two billion lines and finished, and the last one was `255.255.255.254/31`.
- Auto mode stopped the assistant from creating the public GitHub repository at first. The code
  was written in a staging copy, and then moved into the repository once I said to go ahead.
- Onyx needed the VPN again before the portability check could run.
