# Release checklist

Nothing has been merged to `main`, posted to Kaggle, or published outside the feature branch
`claude/vibrant-keller-igafar`. These are the gates to clear first.

| # | Gate | Status | Notes |
|---|---|---|---|
| 1 | Architecture documented | Done, awaiting review | `ARCHITECTURE.md` |
| 2 | Project documentation | Done, awaiting review | `README.md`, `VERIFICATION.md`, `KNOWN_ISSUES.md`, `blazingmq.md`, `backtest_results.md`, `research_report_synthetic.md`, `../dashboard/README.md` |
| 3 | CI green on GitHub | Done for the pushed commits | Four jobs (g++, clang++, ASan+UBSan, research harness). Re-check after the documentation commit. |
| 4 | Claims in the README match the evidence | Done once, review again | Throughput is given as a median and range; synthetic data is stated where results appear. |
| 5 | LICENSE | **Open** | Needs your choice of licence and the copyright holder's name. |
| 6 | Authorship of commits | **Open** | Currently `Claude <noreply@anthropic.com>`. Say if you want your own name and email; that is one more history rewrite. |
| 7 | Repository visibility | **Unknown** | Recruiters and Kaggle readers need to be able to open it. The dashboard header links to the repo. |
| 8 | Merge to `main` or open a pull request | **Not started** | Waiting for your go-ahead. |
| 9 | Real data | **Open, optional** | Needs a Bloomberg Terminal; see `KNOWN_ISSUES.md` item 4. Until then every result is labelled synthetic. |
| 10 | Kaggle target | **Open** | See below. |

## Kaggle

Two facts block this regardless of what you want to post:

- **Network.** The development container's proxy refuses `www.kaggle.com` (HTTP 403), so nothing can be uploaded or
  checked from here. To change that, edit the environment's Network access (cloud environment menu in the session's
  title bar, then Edit) to a broader level, or add `www.kaggle.com` under Allowed domains. The steps are at
  https://code.claude.com/docs/en/cloud-environments#network-access.
- **Credentials.** There is no Kaggle API token in the environment. Uploading with the Kaggle CLI needs a token from
  your Kaggle account, and I will not ask you to paste one into the chat.

What I need from you to proceed:

- Which competition or repository you mean. A competition submission is a predictions file scored on the competition's
  own data. This project's results are on synthetic data and are not a competition entry. If you have a specific
  competition, the useful artefact is a notebook that applies `tools/research.py` and the feature code to that data.
  Otherwise the realistic options are a Kaggle Dataset or Notebook that presents the project, or a GitHub repository
  you name.
- Whether to proceed without gate 5 (licence) and gate 6 (authorship).
