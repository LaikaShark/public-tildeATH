# ~ATH examples

Programs are grouped by topic. Single-file examples are `<name>.ath`; multi-file
ones are a directory with `main.ath` plus any helper functions it imports.

Build and run any of them:

```
python -m athc.cli examples/basics/hello.ath -o hello && ./hello
```

`tests/test_conformance.py` compiles and runs each example under both composition
modes (`fresh` and `intern`) and is the executable spec for their output.

## Topics

- **basics/** — first programs and core syntax: `hello`, `echo`, `multi_word_import`,
  `print_interpolation`, `function_hello`, `identity`, `inline_literals`.
- **object_model/** — composition, decomposition, and object identity: `countdown`,
  `index_snapshot`, `entangle_vs_bifurcate`.
- **control_flow/** — loops and conditionals: `looptest`, `inverted_loop`,
  `repeat_loop`, `every_loop`, `branch`, `execute_postfix`, `execute_hook`.
- **numbers/** — the numeric tower: `arithmetic`, `addition`, `bignum`,
  `bignum_factorial`, `float_arithmetic`, `float_transcendentals`, `numeric_ops`,
  `list_ops`, `comparison`, `verdicts`, `random`.
- **strings/** — string operations on cons-lists: `string_ops`, `string_build`,
  `string_polish`, `string_predicates`, `string_search`, `string_transforms`,
  `first_char`, `text_demo`, `search_replace`, `rot13`.
- **liveness/** — lifetimes, watches, timers, and dynamic death: `timer`,
  `short_lived`, `daemon`, `signal_handler`, `watch_sources`, `file_watcher`,
  `once_runner`, `instant_skipper`, `universe_ends`, `or_dynamic`,
  `lifetime_combinators`.
- **io/** — file and stream I/O: `file_io`, `file_io_owned`, `grep`, `wc`.
- **actors/** — cooperative concurrency (basic → advanced): `hello_actor`,
  `yield_interleave`, `mailbox`, `producer_consumer`, `fan_out`, `fork_join`,
  `ping_pong`, `supervisor_cancel`, `pipeline`, `actor_sleep`.
- **net/** — networking, where a connection is a channel whose liveness is the
  socket: `echo_unix` (echo back each line), `upper_unix` (uppercase each line),
  `calc_unix` (a sum request/response protocol), `chat_unix` (a relay
  multiplexing two clients, one handler actor per connection, into one
  transcript). Each has a `*_tcp` twin (`echo_tcp`, `upper_tcp`, `calc_tcp`,
  `chat_tcp`) that swaps the Unix path for a TCP port (`127.0.0.1:9101`–`9104`)
  and otherwise behaves identically — runnable by hand (or against `nc`), but
  kept out of the conformance suite since a real port isn't deterministic in CI.
- **programs/** — complete applications and algorithms: `calculator`, `maze`,
  `sudoku`, `brainfuck`, `rule110`, `fizzbuzz`, `primes`, `collatz`, `modexp`,
  `newton_sqrt`, `guess`, `wordfreq`, `balanced`, `rle`.
