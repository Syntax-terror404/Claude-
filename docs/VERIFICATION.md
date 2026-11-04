# Verification

What is tested, what each test does and does not prove, and what has never been exercised.
Run everything with `ctest --test-dir build --output-on-failure`.

## Test matrix

| Test (target) | Asserts | Does not assert |
|---|---|---|
| `order_book` (`lob_tests`) | Scenario tests for resting, crossing, price-time priority, price improvement, IOC/FOK/market, cancel, modify, rejects. Differential fuzz against a brute-force reference book: 8 seeds x 20,000 operations, comparing every trade (taker, maker, price, quantity), every cancel result, best bid and ask, resting-order count, and that the book is never crossed. | Internal consistency of the index and level totals (only indirect). Modify is not fuzzed. No concurrent use. |
| `feed` (`feed_tests`) | For 10 seeds with 1% to 10% loss, 1% duplication and reordering inside a window of 6 packets, the handler's book equals a book fed the clean stream at every price level on both sides (price, quantity, order count) and the resting count, and `next_expected` and `applied` are correct. An unrecoverable gap stalls at the gap. | Reordering by more than 6 positions. A real retransmission protocol: the callback reads an in-process copy of the feed. |
| `wire` (`wire_tests`) | 5,000 messages round-trip exactly. Every single flipped bit and every truncation is rejected. Over real loopback UDP, five runs at 1% to 5% loss with garbage datagrams injected rebuild a book equal to the golden book. | Multi-host networks, real packet loss, multicast, packet sizes other than 40 bytes. |
| `bbg_translate` (`bbg_translate_tests`) | Quote/trade ticks become correctly sequenced messages; partial updates keep the last valid value; crossed quotes are dropped and leave cached state untouched; trades hit synthetic liquidity at the right price and side. | Anything about the live Bloomberg API. |
| `features` (`feature_tests`) | Labels equal exactly `mid[i+h] - mid[i]`. Features computed on a truncated stream equal the full-run values at three cut points (no lookahead). Value ranges are sane and the CSV drops the unlabelled tail. | That the features carry signal. |
| `backtest` (`backtest_tests`) | Exact accounting on a scripted feed (+10 gross, 6 fees, +4 net), latency timing, queue position behind an earlier order, maker rebate, position limit, bit-for-bit repeatable runs, and prefix runs reproducing the equity curve. | Market impact, multiple instruments, profitability. |
| `research.py --selftest` | Over 30 seeds the harness finds a planted signal 30 times and produces 0 false positives on both pure noise and a shifted-label control at the 0.05 level. No train/test overlap. | Power against weak real signals. |

All C++ tests also pass under AddressSanitizer and UndefinedBehaviorSanitizer and with both g++ and clang++.

## Continuous integration

`.github/workflows/ci.yml` has four jobs: build and test with g++, the same with clang++ (both Release with
`-Wall -Wextra -Wpedantic`), an ASan and UBSan build running the full suite, and the research harness self-test
followed by an end-to-end run on freshly recorded features. All five runs on the feature branch succeeded on GitHub,
including the latest, and the run on the latest commit covered all four jobs.

CI does not build or run BlazingMQ, the Bloomberg source, or the dashboard.

## Exercised by hand, not in CI

| What | How | Result |
|---|---|---|
| BlazingMQ transport | Local dev broker, `bmq_publish` then `bmq_consume`, compare with golden book | 20,000 and 500,000 messages matched; a stress run with a 1,000-message queue limit lost 13.7% of posts and matched after gap recovery. Details in `blazingmq.md`. |
| Sanitizers on the BlazingMQ tools | Not run | |
| Dashboard rendering | Headless Chromium with a stand-in for the dashboard runtime and the real CSVs, at 1200 px (light and dark) and 400 px | No console errors, no sideways overflow. The real viewer's own check never reported, so rendering there is unconfirmed. |

## Never exercised

- `src/blpapi_source.cpp` has never been compiled or run. It needs the BLPAPI C++ SDK and a logged-in Terminal.
  `tools/bbg_fetch.py` has not been run either.
- No result in the repository comes from real market data.
- Nothing has been run on a machine other than the cloud container used for development.

## How to reproduce the numbers

| Claim | Command |
|---|---|
| Book throughput and latency | `./build/lob_bench` |
| Feed handler under impairment | `./build/pipeline 1000000` |
| Strategy results | `./build/backtest 1000000 10 42` and `./build/backtest 300000 10 <seed>` |
| Research results | `./build/record_features 1000000 f.csv && python3 tools/research.py f.csv --horizon 50` |
| Dashboard datasets | `python3 tools/dashboard_data.py` |

Deterministic outputs (equity, depth, seeds, research) reproduce byte for byte. Benchmark numbers vary with the machine
and its load.
