# Submission Report

- Submission generated at 10/28/2026 at 16:51:00

- Machine info: Linux runnervm48cm3 7.0.0-1017-azure #17~24.04.1-Ubuntu SMP PREEMPT Thu Sep 24 18:46:57 UTC 2026 x86_64 x86_64 x86_64 GNU/Linux

## Note to Students

Please read this report carefully before submission.
Ensure that all sections are complete and accurate.
Look for any errors in the build or test outputs.
If you find any issues, correct them before submitting.
Post any questions on the class discussion board for help.


---

## README

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

All of `src/lab.c`, `src/main.c` and `tests/lab-test.c`, and most of this README, were written by an AI
assistant (Claude) at my request. `src/lab.h` and `tables/example.txt` are copied from the
handout. My AI ran the builds, tests, coverage and leak checks in my WSL environment and
on Onyx.

## Experience

I asked Claude to write the code and run the checks, the same way I did P2.

The most intuitive part of this project was the subnet math because I had already done it by hand for Onyx in A4.
An address is one 32-bit number, the mask keeps the network bits, and the broadcast address is
just the network with every host bit turned on. The program prints the same numbers I tested out on
paper.

What happened along the way:

- The handout warned that the obvious mask for `/0` shifts by 32, which C does not define. Claude
  checked it in WSL first. On my machine the bad version gives back all ones, so the
  default route would only match `0.0.0.0`. There is a test for it now.
- One error message was wrong. `split 192.168.1.0/24 4294967296` said the count was not a power
  of two, but 2^32 is one. It just does not fit in 32 bits. The message now gives the allowed
  range.
- The handout says splitting `0.0.0.0/0` into 2^31 pieces is a legal request. We ran it. It
  printed about two billion lines and finished, and the last one was `255.255.255.254/31`.
- Auto mode stopped the assistant from creating the public GitHub repository at first. The code
  was written in a staging copy, and then moved into the repository once I said to go ahead.
- Onyx needed the VPN again before the portability check could run.

---


## Build Output

This section was generated by running `make all` in the project root directory.

```bash
make[1]: Entering directory '/home/runner/work/cs425-p3/cs425-p3'
mkdir -p build/debug
cc -g -O0 -DDEBUG -fno-omit-frame-pointer -fsanitize=address -c src/lab.c -o build/debug/lab.c.o
mkdir -p build/debug
cc -g -O0 -DDEBUG -fno-omit-frame-pointer -fsanitize=address -c src/main.c -o build/debug/main.c.o
cc -g -O0 -DDEBUG -fno-omit-frame-pointer -fsanitize=address build/debug/lab.c.o build/debug/main.c.o -o build/debug/myapp_d -fsanitize=address
make[1]: Leaving directory '/home/runner/work/cs425-p3/cs425-p3'
make[1]: Entering directory '/home/runner/work/cs425-p3/cs425-p3'
mkdir -p build/release
cc -Wall -Wextra -O2 -fPIE -MMD -MP -Wformat -Wformat=2 -Wconversion -Wsign-conversion -Wimplicit-fallthrough -fstack-protector-strong -Werror=format-security -Werror=implicit -Werror=incompatible-pointer-types -Werror=int-conversion -c src/lab.c -o build/release/lab.c.o
mkdir -p build/release
cc -Wall -Wextra -O2 -fPIE -MMD -MP -Wformat -Wformat=2 -Wconversion -Wsign-conversion -Wimplicit-fallthrough -fstack-protector-strong -Werror=format-security -Werror=implicit -Werror=incompatible-pointer-types -Werror=int-conversion -c src/main.c -o build/release/main.c.o
cc -Wall -Wextra -O2 -fPIE -MMD -MP -Wformat -Wformat=2 -Wconversion -Wsign-conversion -Wimplicit-fallthrough -fstack-protector-strong -Werror=format-security -Werror=implicit -Werror=incompatible-pointer-types -Werror=int-conversion build/release/lab.c.o build/release/main.c.o -o build/release/myapp 
make[1]: Leaving directory '/home/runner/work/cs425-p3/cs425-p3'
make[1]: Entering directory '/home/runner/work/cs425-p3/cs425-p3'
mkdir -p build/tests
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c src/lab.c -o build/tests/lab.c.o
mkdir -p build/tests
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c src/main.c -o build/tests/main.c.o
mkdir -p build/tests/harness/
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c tests/harness/unity.c -o build/tests/harness/unity.c.o
mkdir -p build/tests/
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage -c tests/lab-test.c -o build/tests/lab-test.c.o
cc -g -O0 -DTEST -fprofile-arcs -ftest-coverage build/tests/lab.c.o build/tests/main.c.o build/tests/harness/unity.c.o build/tests/lab-test.c.o -o build/tests/myapp_t -fprofile-arcs -ftest-coverage
make[1]: Leaving directory '/home/runner/work/cs425-p3/cs425-p3'
make[1]: Entering directory '/home/runner/work/cs425-p3/cs425-p3'
mkdir -p build/debug-test
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c src/lab.c -o build/debug-test/lab.c.o
mkdir -p build/debug-test
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c src/main.c -o build/debug-test/main.c.o
mkdir -p build/debug-test/harness/
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c tests/harness/unity.c -o build/debug-test/harness/unity.c.o
mkdir -p build/debug-test/
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address -c tests/lab-test.c -o build/debug-test/lab-test.c.o
cc -g -O0 -DDEBUG -DTEST -fno-omit-frame-pointer -fsanitize=address build/debug-test/lab.c.o build/debug-test/main.c.o build/debug-test/harness/unity.c.o build/debug-test/lab-test.c.o -o build/debug-test/myapp_td -fsanitize=address
make[1]: Leaving directory '/home/runner/work/cs425-p3/cs425-p3'
Builds completed. You can run the application with: ./build/release/myapp
You can run the debug build with: ./build/debug/myapp_d
You can run the test build with: ./build/tests/myapp_t
You can run the debug-test build with: ./build/debug-test/myapp_td
```

---

## Coverage Report

This section was generated by running `make report` in the project root directory.

```bash
tests/lab-test.c:1023:test_ip_parse_valid:PASS
tests/lab-test.c:1024:test_ip_parse_boundaries:PASS
tests/lab-test.c:1025:test_ip_parse_rejects_malformed:PASS
tests/lab-test.c:1026:test_ip_parse_null:PASS
tests/lab-test.c:1027:test_ip_format:PASS
tests/lab-test.c:1028:test_ip_round_trip:PASS
tests/lab-test.c:1029:test_cidr_parse_valid:PASS
tests/lab-test.c:1030:test_cidr_parse_boundaries:PASS
tests/lab-test.c:1031:test_cidr_parse_rejects_malformed:PASS
tests/lab-test.c:1032:test_cidr_parse_null:PASS
tests/lab-test.c:1033:test_cidr_format:PASS
tests/lab-test.c:1034:test_cidr_format_boundaries:PASS
tests/lab-test.c:1035:test_cidr_mask:PASS
tests/lab-test.c:1036:test_cidr_mask_boundaries:PASS
tests/lab-test.c:1037:test_cidr_mask_every_prefix:PASS
tests/lab-test.c:1038:test_onyx_subnet:PASS
tests/lab-test.c:1039:test_cidr_ranges_ordinary:PASS
tests/lab-test.c:1040:test_cidr_ranges_slash0:PASS
tests/lab-test.c:1041:test_cidr_ranges_slash31:PASS
tests/lab-test.c:1042:test_cidr_ranges_slash32:PASS
tests/lab-test.c:1043:test_cidr_ranges_extreme_addresses:PASS
tests/lab-test.c:1044:test_cidr_host_count_every_prefix:PASS
tests/lab-test.c:1045:test_cidr_prefix_over_32_is_32:PASS
tests/lab-test.c:1046:test_cidr_contains:PASS
tests/lab-test.c:1047:test_cidr_contains_boundaries:PASS
tests/lab-test.c:1048:test_cidr_is_network:PASS
tests/lab-test.c:1049:test_cidr_subnet_handout_example:PASS
tests/lab-test.c:1050:test_cidr_subnet_one_is_the_block:PASS
tests/lab-test.c:1051:test_cidr_subnet_boundaries:PASS
tests/lab-test.c:1052:test_cidr_subnet_covers_block_in_order:PASS
tests/lab-test.c:1053:test_cidr_subnet_rejects:PASS
tests/lab-test.c:1054:test_cidr_subnet_prefix_over_32:PASS
tests/lab-test.c:1055:test_route_parse_gateway:PASS
tests/lab-test.c:1056:test_route_parse_default_and_direct:PASS
tests/lab-test.c:1057:test_route_parse_whitespace:PASS
tests/lab-test.c:1058:test_route_parse_device_length:PASS
tests/lab-test.c:1059:test_route_parse_rejects:PASS
tests/lab-test.c:1060:test_route_parse_null:PASS
tests/lab-test.c:1061:test_table_create_destroy:PASS
tests/lab-test.c:1062:test_table_count_null:PASS
tests/lab-test.c:1063:test_table_add:PASS
tests/lab-test.c:1064:test_table_add_rejects:PASS
tests/lab-test.c:1065:test_table_add_two_defaults:PASS
tests/lab-test.c:1066:test_table_add_same_network_other_prefix:PASS
tests/lab-test.c:1067:test_table_add_grows:PASS
tests/lab-test.c:1068:test_table_lookup_longest_prefix:PASS
tests/lab-test.c:1069:test_table_lookup_order_does_not_matter:PASS
tests/lab-test.c:1070:test_table_lookup_no_default_route:PASS
tests/lab-test.c:1071:test_table_lookup_boundaries:PASS
tests/lab-test.c:1072:test_table_lookup_only_default:PASS
tests/lab-test.c:1073:test_table_lookup_null:PASS
tests/lab-test.c:1074:test_table_load_example_file:PASS
tests/lab-test.c:1075:test_table_load_comments_and_blanks:PASS
tests/lab-test.c:1076:test_table_load_empty_file:PASS
tests/lab-test.c:1077:test_table_load_no_final_newline:PASS
tests/lab-test.c:1078:test_table_load_bad_line_in_middle:PASS
tests/lab-test.c:1079:test_table_load_bad_line_counts_comments:PASS
tests/lab-test.c:1080:test_table_load_bad_lines_of_each_kind:PASS
tests/lab-test.c:1081:test_table_load_bad_first_line_without_bad_line:PASS
tests/lab-test.c:1082:test_table_load_into_existing_table:PASS
tests/lab-test.c:1083:test_table_load_null:PASS
tests/lab-test.c:1084:test_table_load_read_error:PASS

-----------------------
62 Tests 0 Failures 0 Ignored 
OK
./build/tests/myapp_t
tests/lab-test.c:1023:test_ip_parse_valid:PASS
tests/lab-test.c:1024:test_ip_parse_boundaries:PASS
tests/lab-test.c:1025:test_ip_parse_rejects_malformed:PASS
tests/lab-test.c:1026:test_ip_parse_null:PASS
tests/lab-test.c:1027:test_ip_format:PASS
tests/lab-test.c:1028:test_ip_round_trip:PASS
tests/lab-test.c:1029:test_cidr_parse_valid:PASS
tests/lab-test.c:1030:test_cidr_parse_boundaries:PASS
tests/lab-test.c:1031:test_cidr_parse_rejects_malformed:PASS
tests/lab-test.c:1032:test_cidr_parse_null:PASS
tests/lab-test.c:1033:test_cidr_format:PASS
tests/lab-test.c:1034:test_cidr_format_boundaries:PASS
tests/lab-test.c:1035:test_cidr_mask:PASS
tests/lab-test.c:1036:test_cidr_mask_boundaries:PASS
tests/lab-test.c:1037:test_cidr_mask_every_prefix:PASS
tests/lab-test.c:1038:test_onyx_subnet:PASS
tests/lab-test.c:1039:test_cidr_ranges_ordinary:PASS
tests/lab-test.c:1040:test_cidr_ranges_slash0:PASS
tests/lab-test.c:1041:test_cidr_ranges_slash31:PASS
tests/lab-test.c:1042:test_cidr_ranges_slash32:PASS
tests/lab-test.c:1043:test_cidr_ranges_extreme_addresses:PASS
tests/lab-test.c:1044:test_cidr_host_count_every_prefix:PASS
tests/lab-test.c:1045:test_cidr_prefix_over_32_is_32:PASS
tests/lab-test.c:1046:test_cidr_contains:PASS
tests/lab-test.c:1047:test_cidr_contains_boundaries:PASS
tests/lab-test.c:1048:test_cidr_is_network:PASS
tests/lab-test.c:1049:test_cidr_subnet_handout_example:PASS
tests/lab-test.c:1050:test_cidr_subnet_one_is_the_block:PASS
tests/lab-test.c:1051:test_cidr_subnet_boundaries:PASS
tests/lab-test.c:1052:test_cidr_subnet_covers_block_in_order:PASS
tests/lab-test.c:1053:test_cidr_subnet_rejects:PASS
tests/lab-test.c:1054:test_cidr_subnet_prefix_over_32:PASS
tests/lab-test.c:1055:test_route_parse_gateway:PASS
tests/lab-test.c:1056:test_route_parse_default_and_direct:PASS
tests/lab-test.c:1057:test_route_parse_whitespace:PASS
tests/lab-test.c:1058:test_route_parse_device_length:PASS
tests/lab-test.c:1059:test_route_parse_rejects:PASS
tests/lab-test.c:1060:test_route_parse_null:PASS
tests/lab-test.c:1061:test_table_create_destroy:PASS
tests/lab-test.c:1062:test_table_count_null:PASS
tests/lab-test.c:1063:test_table_add:PASS
tests/lab-test.c:1064:test_table_add_rejects:PASS
tests/lab-test.c:1065:test_table_add_two_defaults:PASS
tests/lab-test.c:1066:test_table_add_same_network_other_prefix:PASS
tests/lab-test.c:1067:test_table_add_grows:PASS
tests/lab-test.c:1068:test_table_lookup_longest_prefix:PASS
tests/lab-test.c:1069:test_table_lookup_order_does_not_matter:PASS
tests/lab-test.c:1070:test_table_lookup_no_default_route:PASS
tests/lab-test.c:1071:test_table_lookup_boundaries:PASS
tests/lab-test.c:1072:test_table_lookup_only_default:PASS
tests/lab-test.c:1073:test_table_lookup_null:PASS
tests/lab-test.c:1074:test_table_load_example_file:PASS
tests/lab-test.c:1075:test_table_load_comments_and_blanks:PASS
tests/lab-test.c:1076:test_table_load_empty_file:PASS
tests/lab-test.c:1077:test_table_load_no_final_newline:PASS
tests/lab-test.c:1078:test_table_load_bad_line_in_middle:PASS
tests/lab-test.c:1079:test_table_load_bad_line_counts_comments:PASS
tests/lab-test.c:1080:test_table_load_bad_lines_of_each_kind:PASS
tests/lab-test.c:1081:test_table_load_bad_first_line_without_bad_line:PASS
tests/lab-test.c:1082:test_table_load_into_existing_table:PASS
tests/lab-test.c:1083:test_table_load_null:PASS
tests/lab-test.c:1084:test_table_load_read_error:PASS

-----------------------
62 Tests 0 Failures 0 Ignored 
OK
mkdir -p ./build/report/html
mkdir -p ./build/report/txt
gcovr -r . --html --html-details --exclude-directories build/tests/harness --exclude '.*main\.c$' --exclude '.*test\.c$' -o ./build/report/html/coverage_report.html
gcovr -r . --txt                 --exclude-directories build/tests/harness --exclude '.*main\.c$' --exclude '.*test\.c$'
------------------------------------------------------------------------------
                           GCC Code Coverage Report
Directory: .
------------------------------------------------------------------------------
File                                         Lines     Exec  Cover   Missing
------------------------------------------------------------------------------
src/lab.c                                      211      211   100%
------------------------------------------------------------------------------
TOTAL                                          211      211   100%
------------------------------------------------------------------------------
```

---

## Address Sanitizer Report

This section was generated by running `make leak-test` in the project root directory.

```bash
tests/lab-test.c:1023:test_ip_parse_valid:PASS
tests/lab-test.c:1024:test_ip_parse_boundaries:PASS
tests/lab-test.c:1025:test_ip_parse_rejects_malformed:PASS
tests/lab-test.c:1026:test_ip_parse_null:PASS
tests/lab-test.c:1027:test_ip_format:PASS
tests/lab-test.c:1028:test_ip_round_trip:PASS
tests/lab-test.c:1029:test_cidr_parse_valid:PASS
tests/lab-test.c:1030:test_cidr_parse_boundaries:PASS
tests/lab-test.c:1031:test_cidr_parse_rejects_malformed:PASS
tests/lab-test.c:1032:test_cidr_parse_null:PASS
tests/lab-test.c:1033:test_cidr_format:PASS
tests/lab-test.c:1034:test_cidr_format_boundaries:PASS
tests/lab-test.c:1035:test_cidr_mask:PASS
tests/lab-test.c:1036:test_cidr_mask_boundaries:PASS
tests/lab-test.c:1037:test_cidr_mask_every_prefix:PASS
tests/lab-test.c:1038:test_onyx_subnet:PASS
tests/lab-test.c:1039:test_cidr_ranges_ordinary:PASS
tests/lab-test.c:1040:test_cidr_ranges_slash0:PASS
tests/lab-test.c:1041:test_cidr_ranges_slash31:PASS
tests/lab-test.c:1042:test_cidr_ranges_slash32:PASS
tests/lab-test.c:1043:test_cidr_ranges_extreme_addresses:PASS
tests/lab-test.c:1044:test_cidr_host_count_every_prefix:PASS
tests/lab-test.c:1045:test_cidr_prefix_over_32_is_32:PASS
tests/lab-test.c:1046:test_cidr_contains:PASS
tests/lab-test.c:1047:test_cidr_contains_boundaries:PASS
tests/lab-test.c:1048:test_cidr_is_network:PASS
tests/lab-test.c:1049:test_cidr_subnet_handout_example:PASS
tests/lab-test.c:1050:test_cidr_subnet_one_is_the_block:PASS
tests/lab-test.c:1051:test_cidr_subnet_boundaries:PASS
tests/lab-test.c:1052:test_cidr_subnet_covers_block_in_order:PASS
tests/lab-test.c:1053:test_cidr_subnet_rejects:PASS
tests/lab-test.c:1054:test_cidr_subnet_prefix_over_32:PASS
tests/lab-test.c:1055:test_route_parse_gateway:PASS
tests/lab-test.c:1056:test_route_parse_default_and_direct:PASS
tests/lab-test.c:1057:test_route_parse_whitespace:PASS
tests/lab-test.c:1058:test_route_parse_device_length:PASS
tests/lab-test.c:1059:test_route_parse_rejects:PASS
tests/lab-test.c:1060:test_route_parse_null:PASS
tests/lab-test.c:1061:test_table_create_destroy:PASS
tests/lab-test.c:1062:test_table_count_null:PASS
tests/lab-test.c:1063:test_table_add:PASS
tests/lab-test.c:1064:test_table_add_rejects:PASS
tests/lab-test.c:1065:test_table_add_two_defaults:PASS
tests/lab-test.c:1066:test_table_add_same_network_other_prefix:PASS
tests/lab-test.c:1067:test_table_add_grows:PASS
tests/lab-test.c:1068:test_table_lookup_longest_prefix:PASS
tests/lab-test.c:1069:test_table_lookup_order_does_not_matter:PASS
tests/lab-test.c:1070:test_table_lookup_no_default_route:PASS
tests/lab-test.c:1071:test_table_lookup_boundaries:PASS
tests/lab-test.c:1072:test_table_lookup_only_default:PASS
tests/lab-test.c:1073:test_table_lookup_null:PASS
tests/lab-test.c:1074:test_table_load_example_file:PASS
tests/lab-test.c:1075:test_table_load_comments_and_blanks:PASS
tests/lab-test.c:1076:test_table_load_empty_file:PASS
tests/lab-test.c:1077:test_table_load_no_final_newline:PASS
tests/lab-test.c:1078:test_table_load_bad_line_in_middle:PASS
tests/lab-test.c:1079:test_table_load_bad_line_counts_comments:PASS
tests/lab-test.c:1080:test_table_load_bad_lines_of_each_kind:PASS
tests/lab-test.c:1081:test_table_load_bad_first_line_without_bad_line:PASS
tests/lab-test.c:1082:test_table_load_into_existing_table:PASS
tests/lab-test.c:1083:test_table_load_null:PASS
tests/lab-test.c:1084:test_table_load_read_error:PASS

-----------------------
62 Tests 0 Failures 0 Ignored 
OK
```

---

## Src Files
### lab.c

```c

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

```

### lab.h

```c

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

```

### main.c

```c

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

```

## Tests Files
### lab-test.c

```c

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

```

## Scripts Files
Report generated on 10/28/2026 at 16:51:03


---

## End of Report

SHA-256 Hash of the report: 33fd4117e9501a69a4d4d66fd8146e6a128131a6307409f7cdfece2fa2d48841

Do not edit the generated report. Any changes will be reported as academic dishonesty

---
## GitHub Info
- GitHub repo name: RandyBauer/cs425-p3
- The repository visibility is public.
- The workflow was triggered by RandyBauer
