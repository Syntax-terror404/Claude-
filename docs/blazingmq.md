# BlazingMQ transport

`examples/bmq_publish.cpp` posts the synthetic feed to a BlazingMQ queue as 40-byte wire packets;
`examples/bmq_consume.cpp` reads them into the feed handler and order book, then compares the result
with a golden book built from the same deterministic feed. Both are opt-in (`-DLOB_WITH_BMQ=ON`) and not
part of CI, because BlazingMQ takes about an hour to build from source.

## Build and run

```bash
# one-time: build BlazingMQ (clones BDE and NTF, needs flex/bison/libfl-dev)
git clone --depth 1 https://github.com/bloomberg/blazingmq.git && cd blazingmq && bash bin/build-ubuntu.sh
# start the dev broker (listens on tcp://localhost:30114)
cd build/blazingmq/src/applications/bmqbrkr && ./run

# build and run our tools
cmake -S . -B build-bmq -DLOB_WITH_BMQ=ON -DBMQ_ROOT=/path/to/blazingmq
cmake --build build-bmq --target bmq_publish bmq_consume
Q=bmq://bmq.test.mem.priority/feed-demo
./build-bmq/bmq_consume $Q 20000 42 &        # <queue> <expected messages> <seed>
./build-bmq/bmq_publish $Q 20000 42          # <queue> <messages> <seed>
```

## Results (4-vCPU VM, dev broker, in-memory queue)

| run | published / acked | received | retransmits | book vs golden |
|---|---|---|---|---|
| 20,000 msgs, queue limit raised to 1M | 20,000 / 20,000 | 20,000 | 0 | **match** |
| 500,000 msgs, queue limit raised to 1M | 500,000 / 500,000 | 500,000 | 0 | **match** |
| 20,000 msgs, default test-domain limit (1,000 messages) | 17,257 / 20,000 | 17,257 | 2,743 | match (see below) |

- **Throughput is about 24k msgs/s publish+ack** for the 500k run. That is bounded by a debug-configured
  broker and client (the BlazingMQ tree was built with `BDE_BUILD_TARGET_DBG`, `-g`, assertions on) sharing
  four vCPUs with the consumer. It says nothing about BlazingMQ's production performance.
- **The third row is a stress case, not a clean result.** With the stock 1,000-message queue limit the broker
  rejected 2,743 posts (`LIMIT_MESSAGES`) because the consumer confirmed more slowly than the producer
  posted. The feed handler detected the sequence gaps and filled them via its retransmit callback. That
  callback reads an in-process copy of the feed, i.e. it **simulates** a retransmission service; BlazingMQ
  did not retransmit anything. What it demonstrates is that the handler's gap recovery works on real
  transport loss, not that BlazingMQ is lossy by design.
- The producer uses a 500-message send window on ACKs. ACKs only bound what the broker has accepted, not
  what the consumer has confirmed, so queue limits still matter. Size them for your producer/consumer rates.

## Notes

- BlazingMQ is built as C++23 with BDE debug settings; the CMake option mirrors those defines (`BDE_BUILD_TARGET_DBG`,
  `BSLS_ASSERT_LEVEL_ASSERT_SAFE`) so the ABI matches the prebuilt libraries.
- Messages need a correlation id when the queue is opened with `e_ACK`; otherwise `packMessage` fails.
