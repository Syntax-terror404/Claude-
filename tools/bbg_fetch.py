"""Fetch daily history from Bloomberg Desktop API (//blp/refdata) to CSV.

Run on a machine with a logged-in Terminal and `blpapi` installed
(pip install --index-url=https://blpapi.bloomberg.com/repository/releases/python/simple blpapi).
NOT tested against a live Terminal. Mind your data licence before sharing output.
Usage: python bbg_fetch.py "AAPL US Equity" 20260101 20261101 out.csv
"""
import csv, sys, blpapi

sec, start, end, out = sys.argv[1:5]
opts = blpapi.SessionOptions(); opts.setServerHost("localhost"); opts.setServerPort(8194)
s = blpapi.Session(opts)
if not s.start() or not s.openService("//blp/refdata"):
    sys.exit("cannot reach Bloomberg API on localhost:8194 (is the Terminal running?)")
req = s.getService("//blp/refdata").createRequest("HistoricalDataRequest")
req.getElement("securities").appendValue(sec)
for f in ("PX_LAST", "PX_VOLUME"): req.getElement("fields").appendValue(f)
req.set("startDate", start); req.set("endDate", end); req.set("periodicitySelection", "DAILY")
s.sendRequest(req)
rows, done = [], False
while not done:
    ev = s.nextEvent(5000)
    for msg in ev:
        if msg.hasElement("securityData"):
            for d in msg.getElement("securityData").getElement("fieldData").values():
                rows.append([d.getElementAsDatetime("date"), d.getElementAsFloat("PX_LAST"),
                             d.getElementAsFloat("PX_VOLUME")])
    done = ev.eventType() == blpapi.Event.RESPONSE
with open(out, "w", newline="") as f:
    w = csv.writer(f); w.writerow(["date", "px_last", "px_volume"]); w.writerows(rows)
print(f"wrote {len(rows)} rows to {out}")
