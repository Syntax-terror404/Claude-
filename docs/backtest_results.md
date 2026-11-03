# Backtest results (synthetic feed)

`./build/backtest <messages> <latency> <seed>` — 300k messages, latency 10 messages, taker fee 0.3 ticks/share, maker fee 0.

| seed | market-maker net (ticks) | imbalance-taker net (ticks) | imbalance-taker gross |
|---|---|---|---|
| 1 | +11,214 | -45,602 | -32,049 |
| 2 | +7,369 | -47,228 | -33,315 |
| 3 | +4,037 | -48,171 | -34,159 |
| 4 | +12,162 | -43,699 | -30,394 |
| 5 | +26,808 | -38,877 | -27,005 |

At 1M messages the market-maker's edge falls from +66,994 at latency 10 to +17,185 at latency 100.

## How to read this

- **Market-maker profit is an artefact of the data, not an edge.** The synthetic flow is uninformed (random orders around a hidden mid), so a passive quoter earns the spread with no adverse selection. Real flow contains informed traders; this result will not transfer.
- **The imbalance signal is statistically predictive but not tradable.** `tools/research.py` finds a positive out-of-sample IC for `imb1`, yet this strategy loses money even *before* fees (gross is negative): the expected move per trade is smaller than the cost of crossing the spread, consistent with the near-zero OOS R² in `research_report_synthetic.md`. Predictability is not profitability.
- **Replay limits:** the feed does not react to our orders (no impact), and fill realism is bounded by the data. See the header of `include/lob/backtest.hpp`.
