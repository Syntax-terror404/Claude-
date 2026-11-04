# Known issues and limitations

Ordered by how much they affect what you can claim about this project.

## Affects the claims

1. **All results come from a synthetic feed.** The generator has no informed traders and leaves stale orders behind as
   the mid drifts, so in a one-million-message run the average spread grows from about 1.6 ticks to about 15 and resting
   depth grows and becomes lopsided (the bid side grew about four-fold in one run). Strategy and signal results describe that generator, not a market.
2. **The market-maker's profit is an artefact of that feed.** Uninformed flow means there is no adverse selection.
3. **The imbalance signal is predictive but not tradable.** Out-of-sample rank correlation is positive and clears the
   null, yet the strategy loses money before fees because the expected move is smaller than the cost of crossing the
   spread. R² against predicting zero is slightly negative.
4. **The Bloomberg source has never been compiled or run.** `src/blpapi_source.cpp` and `tools/bbg_fetch.py` were
   written from knowledge of the BLPAPI C++ and Python APIs without the SDK or a Terminal. Expect fixes on first build.
   It reconstructs top of book from quotes and trades; it does not provide depth.

## Affects the design

5. **No real retransmission protocol.** `FeedHandler` recovers gaps through a callback. The UDP transport has no return
   channel, so in every test the callback reads an in-process copy of the feed. The BlazingMQ stress run did not make
   BlazingMQ retransmit anything.
6. **No periodic heartbeat sender.** The wire format has a heartbeat type and the handler uses it to detect tail loss,
   but nothing in the repository sends heartbeats on a timer; tests send a single one.
7. **UDP is IPv4 unicast only.** No multicast, no IPv6, no pacing.
8. **Order ids.** Strategy orders use ids from `2^60` upward. A real feed that uses ids in that range would collide, and
   nothing checks for it.
9. **The handler's `retransmits` counter counts requests**, including ones the callback refuses.
10. **Single instrument, single thread.** The book is not thread-safe, and there is no lock-free queue. An earlier plan
    listed one; it was not built.
11. **Backtester realism.** The replayed feed does not react to strategy orders, so there is no market impact. Latency is
    a fixed number of messages, not wall-clock time. Self-crossing between two strategy orders is not modelled.

## Affects the evidence

12. **Reordering in tests is local.** The impairment channel shuffles within a window of 6 packets. Larger reordering,
    bursts of loss and a very slow consumer are not covered by the handler tests.
13. **Research harness.** Two models only (ridge regression and a sign rule), one horizon, one dataset. With 20 shifts the
    smallest reportable p-value is about 0.048, so results sit on the edge of the 0.05 threshold by construction. More
    shifts would tighten it.
14. **Benchmarks vary.** On the shared development VM, median throughput moved by about 25% between sessions. Latency
    includes timer overhead and the maximum is scheduler noise.
15. **BlazingMQ numbers come from a debug-built broker and client**, about 24k messages per second round trip. They say
    nothing about BlazingMQ in production. The publisher's send window counts broker acknowledgements, which does not
    bound how far the consumer lags, so the stock 1,000-message queue limit drops posts under load.

## Affects presentation

16. **Dashboard.** It was rendered here only in a headless browser with a stand-in runtime; the real viewer's own check
    never reported. Its datasets are static copies, some headline figures show without thousands separators, and wide
    tables scroll sideways on a phone.
17. **No LICENSE file.** That needs a decision on licence and copyright holder.
18. **Authorship.** Commits are authored as `Claude <noreply@anthropic.com>`, the identity configured in the
    development container. History was rewritten and force-pushed once, to remove co-author lines and restore commit
    signatures, so anyone who cloned the branch before that needs to reset to the remote.
