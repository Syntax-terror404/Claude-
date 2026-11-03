# Dashboard

`index.html` is the page published as the project dashboard (a Claude Dashboard artifact). It has no numbers of
its own: every figure, chart and table is drawn from the CSVs in `data/`, which are attached to the dashboard as
files. The page expects the artifact runtime (`dash.data`, `dash.draw`, `dash.calc`, `d3`), so it will not render
by opening the file directly.

| file | what | how it is produced |
|---|---|---|
| `data/equity.csv` | net equity of both strategies, 1M messages | `./build/backtest 1000000 10 42 equity.csv` (deterministic) |
| `data/depth.csv` | resting depth within 10 ticks of the mid, 100 snapshots | `./build/dump_depth 1000000 100 depth.csv 10 42` (deterministic) |
| `data/seeds.csv` | both strategies, seeds 1 to 5, 300k messages | `./build/backtest 300000 10 <seed>` (deterministic) |
| `data/research.csv` | rank correlation, shuffled-label range, p-value | parsed from `docs/research_report_synthetic.md` |
| `data/latency.csv`, `data/throughput.csv`, `data/pipeline.csv` | benchmark figures | medians of five runs on one machine; **re-measured values differ** (this VM varied by about 25% between sessions) |
| `data/transport.csv` | BlazingMQ verification runs | recorded in `docs/blazingmq.md`; needs a local broker to re-run |

`python3 tools/dashboard_data.py` regenerates all of them. The committed copies are exactly what the published
dashboard shows.

To update the dashboard, re-attach the changed CSV to the artifact and point its dataset at the new file. CSVs must
use plain `\n` line endings (the generator already does this).
