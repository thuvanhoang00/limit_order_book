# Report format

The default human-readable report for this project is a **standalone HTML file**.
This is the user's chosen format for future performance and optimization reports.
It must open locally without a server, account, CDN, or network connection.

Keep the first screen focused on conclusions and decisions:

- What was measured, what passed, and what remains untested.
- Baseline throughput, p50/p99/p99.9/max, peak RAM and observer overhead when measured.
- The bottleneck supported by evidence and the next optimization to evaluate.

Use charts to compare modes and individual repetitions. Distinguish measured data,
estimates and hypotheses. An observer on/off comparison is not an implementation
speedup; an offline replay is not a network or matching-engine latency measurement.
Show units, timer boundaries, sample counts, run-to-run variation and whether the
input was a full session or a limited prefix. Do not subtract clock overhead from
percentiles to claim corrected latency.

Provide sortable/filterable detail tables where useful, particularly for slow
records and CPU hotspots. Keep machine/build metadata, exact commands, raw histogram
links and detailed logs in expandable sections so they remain available without
dominating the summary. Retain JSON/raw measurements as the source of truth.

Markdown is for documentation and concise PR/run summaries linking to the HTML
report. Avoid generating a second full Markdown copy of the same detailed report.
PNG/SVG charts may be retained for GitHub previews and export. Preserve historical
evidence, including files covered by checksum manifests; this convention applies
to new reports rather than rewriting old benchmark snapshots.
