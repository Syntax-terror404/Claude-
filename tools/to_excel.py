"""Convert pipeline CSV snapshots to a formatted .xlsx (with a line chart)."""
import csv, sys
from openpyxl import Workbook
from openpyxl.chart import LineChart, Reference

src, dst = sys.argv[1], sys.argv[2]
wb = Workbook(); ws = wb.active; ws.title = "snapshots"
with open(src) as f:
    for i, row in enumerate(csv.reader(f)):
        ws.append(row if i == 0 else [float(x) for x in row])
n = ws.max_row
ch = LineChart(); ch.title = "Mid price"; ch.height, ch.width = 8, 18
ch.add_data(Reference(ws, min_col=2, min_row=1, max_row=n), titles_from_data=True)
ch.set_categories(Reference(ws, min_col=1, min_row=2, max_row=n))
ws.add_chart(ch, "H2")
wb.save(dst); print(f"wrote {dst} ({n-1} rows)")
