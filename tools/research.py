#!/usr/bin/env python3
"""Research stage: does the recorded order-book feature set predict the forward mid move?

Input : CSV from `record_features` (columns ... label_dmid).
Method: expanding-window walk-forward. Each fold trains on the past only, with a purge gap of
        `horizon` samples between train and test (labels overlap that far into the future).
        Models: ridge regression, plus single-feature rules. Baseline: predict 0.
Metrics (out-of-sample only): Spearman IC, OOS R^2 vs the zero predictor, directional hit
        rate on non-zero moves. Confidence intervals use a moving-block bootstrap because
        overlapping labels are strongly autocorrelated; a naive i.i.d. CI would be far too narrow.
Control: the same pipeline on circularly shifted labels (destroys the link, keeps autocorrelation).
        If the control shows "signal", the harness is broken, not the market.
numpy only (no sklearn) so it also runs in constrained environments such as BQuant.

    python3 tools/research.py features.csv --horizon 50
    python3 tools/research.py --selftest
"""
import argparse
import sys

import numpy as np
import pandas as pd

# Stationary features only. Raw spread/depth LEVELS drift over time (stale orders accumulate in
# the synthetic book), and a model fit on early levels extrapolates badly (observed OOS R2 = -0.87).
N_SHIFTS = 20
BLOCK_MULT = 40  # bootstrap block = BLOCK_MULT * horizon samples (features/regimes persist for long stretches)
FEATURES = ["imb1", "imb5", "mp_norm", "depth_logratio", "flow"]


def prepare(df):
    df = df.copy()
    df["mp_norm"] = df["microprice_dev"] / df["spread"]                # in [-0.5, 0.5]
    df["depth_logratio"] = np.log((df["bid_depth"] + 1.0) / (df["ask_depth"] + 1.0))
    return df


def rank(a):
    """Average ranks for ties. Ordinal ranks would break ties by row position and leak a spurious
    time-order correlation into integer-valued labels and sign rules."""
    _, inv, counts = np.unique(a, return_inverse=True, return_counts=True)
    ends = np.cumsum(counts)
    return ((ends - (counts - 1) / 2.0) - 1.0)[inv]


def spearman(a, b):
    if len(a) < 3 or np.std(a) == 0 or np.std(b) == 0:
        return 0.0
    return float(np.corrcoef(rank(a), rank(b))[0, 1])


def ridge_fit(X, y, lam=10.0):
    mu, sd = X.mean(0), X.std(0)
    sd[sd == 0] = 1.0
    Z = (X - mu) / sd
    ym = y.mean()
    w = np.linalg.solve(Z.T @ Z + lam * np.eye(Z.shape[1]), Z.T @ (y - ym))
    return lambda Xn: ((Xn - mu) / sd) @ w + ym


def walk_forward(X, y, horizon, folds=5, min_train=0.3):
    """Yields (train_idx, test_idx) with a purge gap; train always strictly precedes test."""
    n = len(y)
    start = int(n * min_train)
    edges = np.linspace(start, n, folds + 1).astype(int)
    for k in range(folds):
        test = np.arange(edges[k], edges[k + 1])
        train = np.arange(0, max(0, edges[k] - horizon))
        if len(train) > 100 and len(test) > 20:
            yield train, test


def block_bootstrap_ic(pred, y, block, reps=200, seed=0):
    rng = np.random.default_rng(seed)
    n = len(y)
    nb = max(1, n // block)
    vals = []
    for _ in range(reps):
        starts = rng.integers(0, max(1, n - block), nb)
        idx = np.concatenate([np.arange(s, min(s + block, n)) for s in starts])
        vals.append(spearman(pred[idx], y[idx]))
    return float(np.percentile(vals, 2.5)), float(np.percentile(vals, 97.5))


def evaluate(df, horizon, folds=5, label="label_dmid", ridge_lambda=10.0, reps=200):
    X = df[FEATURES].to_numpy(float)
    y = df[label].to_numpy(float)
    preds = {"ridge": np.full(len(y), np.nan), "sign(imb1)": np.full(len(y), np.nan)}
    for tr, te in walk_forward(X, y, horizon, folds):
        preds["ridge"][te] = ridge_fit(X[tr], y[tr], ridge_lambda)(X[te])
        preds["sign(imb1)"][te] = np.sign(X[te, FEATURES.index("imb1")])
    mask = ~np.isnan(preds["ridge"])
    yt = y[mask]
    out = {"n_test": int(mask.sum())}
    for name, p in preds.items():
        pt = p[mask]
        ic = spearman(pt, yt)
        lo, hi = block_bootstrap_ic(pt, yt, block=max(BLOCK_MULT * horizon, 10), reps=reps) if reps else (float('nan'), float('nan'))
        nz = yt != 0
        hit = float(np.mean(np.sign(pt[nz]) == np.sign(yt[nz]))) if nz.any() else float("nan")
        base = float(np.mean(yt[nz] > 0)) if nz.any() else float("nan")
        sse = float(np.sum((yt - pt) ** 2)) if name == "ridge" else float("nan")
        r2 = 1 - sse / float(np.sum(yt ** 2)) if name == "ridge" else float("nan")
        out[name] = {"ic": ic, "ic_ci": (lo, hi), "hit": hit, "up_rate": base, "oos_r2": r2}
    return out


def significant(res):
    lo, hi = res["ic_ci"]
    return lo > 0 or hi < 0


def report(df, horizon, folds, reps=200):
    df = prepare(df) if "mp_norm" not in df else df
    real = evaluate(df, horizon, folds, reps=reps)
    y = df["label_dmid"].to_numpy()
    n = len(y)
    models = ("ridge", "sign(imb1)")
    null = {k: [] for k in models}
    for f in np.linspace(0.05, 0.95, N_SHIFTS):  # circular shifts: keep autocorrelation, break the link
        shifted = df.copy()
        shifted["label_dmid"] = np.roll(y, int(n * f) + 137)
        c = evaluate(shifted, horizon, folds, reps=0)
        for k in models:
            null[k].append(c[k]["ic"])
    pvals = {k: (1 + sum(abs(v) >= abs(real[k]["ic"]) for v in null[k])) / (1 + N_SHIFTS) for k in models}
    lines = ["# Research report", "",
             f"rows={len(df)}  out-of-sample rows={real['n_test']}  horizon={horizon} samples  folds={folds}", "",
             "| model | IC | null IC range (shifted labels) | p-value | 95% block-bootstrap CI | hit rate (up-rate) | OOS R2 vs zero |",
             "|---|---|---|---|---|---|---|"]
    for k in models:
        r = real[k]
        r2 = "" if np.isnan(r["oos_r2"]) else f"{r['oos_r2']:+.4f}"
        lines.append(f"| {k} | {r['ic']:+.4f} | [{min(null[k]):+.4f}, {max(null[k]):+.4f}] | {pvals[k]:.3f} | "
                     f"[{r['ic_ci'][0]:+.4f}, {r['ic_ci'][1]:+.4f}] | {r['hit']:.3f} ({r['up_rate']:.3f}) | {r2} |")
    lines += ["", f"p-value = (1 + #null runs with |IC| >= |real IC|) / (1 + {N_SHIFTS}); the smallest possible is "
              f"{1 / (1 + N_SHIFTS):.3f}. The null uses {N_SHIFTS} circular label shifts, which respects "
              "long-lived autocorrelation that the bootstrap interval can understate."]
    lines += ["", "Caveat: this measures statistical predictability of the mid move, not tradable profit "
              "(no spread cost, queue position, latency or fees). Synthetic data proves the harness, not a market edge."]
    return "\n".join(lines), real, pvals


def selftest():
    """Exercise the decision rule actually used in reports (empirical-null p-value <= 0.05).
    A level-0.05 test is wrong ~1 time in 20 by construction, so check rates over many seeds."""
    trials = 30
    found = fp_ctrl = fp_noise = 0
    for seed in range(trials):
        rng = np.random.default_rng(seed)
        n = 6000
        X = rng.normal(size=(n, len(FEATURES)))
        df = pd.DataFrame(X, columns=FEATURES)
        df["label_dmid"] = 0.6 * X[:, FEATURES.index("imb1")] + rng.normal(size=n)  # planted signal
        found += report(df, 20, 4, reps=0)[2]["ridge"] <= 0.05
        ctrl = df.copy()
        ctrl["label_dmid"] = np.roll(df["label_dmid"].to_numpy(), n // 2 + 137)
        fp_ctrl += report(ctrl, 20, 4, reps=0)[2]["ridge"] <= 0.05
        noise = df.copy()
        noise["label_dmid"] = rng.normal(size=n)
        fp_noise += report(noise, 20, 4, reps=0)[2]["ridge"] <= 0.05
        for tr, te in walk_forward(X, df["label_dmid"].to_numpy(), 20, 4):  # leakage guard
            assert tr.max() + 20 <= te.min(), "train/test overlap"
    print(f"selftest: planted signal found {found}/{trials}; false positives: "
          f"shifted-control {fp_ctrl}/{trials}, pure-noise {fp_noise}/{trials}")
    assert found == trials, "missed a strong planted signal"
    assert fp_ctrl <= 0.2 * trials and fp_noise <= 0.2 * trials, "false-positive rate too high"
    print("selftest passed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?")
    ap.add_argument("--horizon", type=int, default=50)
    ap.add_argument("--folds", type=int, default=5)
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.csv:
        ap.error("csv required")
    df = prepare(pd.read_csv(a.csv))
    df = df.replace([np.inf, -np.inf], np.nan).dropna(subset=FEATURES + ["label_dmid"]).reset_index(drop=True)
    text, _, _ = report(df, a.horizon, a.folds)
    print(text)
    if a.out:
        open(a.out, "w").write(text + "\n")


if __name__ == "__main__":
    sys.exit(main())
