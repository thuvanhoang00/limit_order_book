#!/usr/bin/env python3
"""Record allocator benchmarks and render normalized CSV, Markdown and charts."""

import argparse
import collections
import csv
from datetime import datetime
import hashlib
import html
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
WORKLOADS = ("FillClearFirstUse", "FillClearReuse", "AddCancelChurn")
LABELS = {
    "FillClearFirstUse": "Fill/clear: first use",
    "FillClearReuse": "Fill/clear: reuse",
    "AddCancelChurn": "Continuous Add/Cancel",
}
BOOKS = ("HeapOrderBook", "OrderBook")
COLORS = ("#64748b", "#0284c7")
PATTERN = re.compile(r"BM_(FillClearFirstUse|FillClearReuse|AddCancelChurn)<(HeapOrderBook|OrderBook)>/(\d+)$")


def now():
    return datetime.now().astimezone().isoformat()


def capture(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def read_optional(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def pyplot():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise ValueError("Charts require matplotlib; install it for this Python environment before running.") from error
    return plt


def parse_results(data):
    """Normalize individual repetition means, never aggregate rows, to ns/event."""
    rows = []
    groups = collections.defaultdict(list)
    for item in data.get("benchmarks", []):
        if item.get("error_occurred"):
            raise ValueError("Benchmark failed: " + item.get("error_message", item["name"]))
        if item.get("run_type") == "aggregate" or "aggregate_name" in item:
            continue
        name = item.get("run_name", item["name"])
        match = PATTERN.fullmatch(name)
        if not match:
            raise ValueError("Cannot determine event count for benchmark: " + name)
        workload, book, count = match.groups()
        count = int(count)
        if count <= 0:
            raise ValueError("Order count must be positive")
        events = count * (8 if workload == "AddCancelChurn" else 2)
        units = {"ns": 1, "us": 1_000, "ms": 1_000_000, "s": 1_000_000_000}
        if item["time_unit"] not in units:
            raise ValueError("Unknown time unit: " + item["time_unit"])
        factor = units[item["time_unit"]] / events
        row = {
            "workload": workload, "implementation": book, "active_orders": count,
            "events_per_batch": events,
            "repetition_index": item.get("repetition_index", 0),
            "iterations": item["iterations"],
            "cpu_ns_per_event": item["cpu_time"] * factor,
            "wall_ns_per_event": item["real_time"] * factor,
            "heap_allocations_per_event": item["heap_allocs/event"],
            "heap_requested_bytes_per_event": item["heap_requested_bytes/event"],
        }
        rows.append(row)
        groups[(workload, book, count)].append(row)
    if not rows:
        raise ValueError("No individual repetitions found; run with --benchmark_report_aggregates_only=false")
    summaries = []
    for (workload, book, count), samples in sorted(groups.items()):
        cpu = [sample["cpu_ns_per_event"] for sample in samples]
        mean = statistics.mean(cpu)
        deviation = statistics.stdev(cpu) if len(cpu) > 1 else 0.0
        summaries.append({
            "workload": workload, "implementation": book, "active_orders": count,
            "events_per_batch": samples[0]["events_per_batch"], "repetitions": len(samples),
            "median_cpu_ns_per_event": statistics.median(cpu), "mean_cpu_ns_per_event": mean,
            "cpu_standard_deviation_ns_per_event": deviation,
            "cpu_cv_percent": deviation / mean * 100 if mean else 0.0,
            "minimum_cpu_ns_per_event": min(cpu), "maximum_cpu_ns_per_event": max(cpu),
            "median_wall_ns_per_event": statistics.median(s["wall_ns_per_event"] for s in samples),
            "heap_allocations_per_event": statistics.median(s["heap_allocations_per_event"] for s in samples),
            "heap_requested_bytes_per_event": statistics.median(s["heap_requested_bytes_per_event"] for s in samples),
        })
    return rows, summaries, groups


def write_csv(path, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def render(directory):
    directory = directory.resolve()
    data = json.loads((directory / "benchmark.json").read_text())
    meta_path = directory / "metadata.json"
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    rows, summaries, groups = parse_results(data)
    plt = pyplot()
    plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})
    lookup = {(r["workload"], r["implementation"], r["active_orders"]): r for r in summaries}
    workloads = [w for w in WORKLOADS if any(key[0] == w for key in lookup)]
    counts = sorted({row["active_orders"] for row in rows})
    source = meta.get("source", {})
    commit = source.get("commit", "unknown")[:12]
    dirty = " + working-tree changes" if source.get("working_tree_clean") is False else ""
    cpu_info = meta.get("system", {}).get("cpu", {}).get("lscpu", [])
    cpu_model = next((item["data"] for item in cpu_info if item["field"].rstrip(":") == "Model name"), "See metadata")
    compiler = meta.get("build", {}).get("compiler", "See metadata").splitlines()[0]
    preset = meta.get("protocol", {}).get("preset", meta.get("build", {}).get("preset", "see CMakeCache.txt"))
    environment = f"{cpu_model}; {compiler}; preset {preset}; logical CPU {meta.get('system', {}).get('measurement_cpu', 'unspecified')}; logging off"
    title = f"Order book CPU cost | {commit}{dirty} | {directory.name}"
    fig, axes = plt.subplots(1, len(workloads), figsize=(5 * len(workloads), 4.8), squeeze=False)
    for axis, workload in zip(axes[0], workloads):
        for book_index, (book, color) in enumerate(zip(BOOKS, COLORS)):
            label = "Heap" if book == "HeapOrderBook" else "Pool"
            for index, count in enumerate(counts):
                summary = lookup.get((workload, book, count))
                if not summary:
                    continue
                position = index + (-0.2 if book_index == 0 else 0.2)
                axis.bar(position, summary["median_cpu_ns_per_event"], width=0.36,
                         yerr=summary["cpu_standard_deviation_ns_per_event"], capsize=4,
                         color=color, alpha=0.8, label=label if index == 0 else None)
                samples = groups[(workload, book, count)]
                jitter = [position + (i - (len(samples) - 1) / 2) * 0.018 for i in range(len(samples))]
                axis.scatter(jitter, [s["cpu_ns_per_event"] for s in samples], s=13,
                             color="#0f172a", alpha=0.55, zorder=3)
                axis.annotate(f"{summary['median_cpu_ns_per_event']:.2f}",
                              (position, summary["median_cpu_ns_per_event"] + summary["cpu_standard_deviation_ns_per_event"]),
                              xytext=(0, 5), textcoords="offset points", ha="center", fontsize=9)
        axis.set_title(LABELS[workload])
        axis.set_xticks(range(len(counts)), [str(n) for n in counts])
        axis.set_xlabel("Active orders")
        axis.set_ylabel("CPU ns/event (lower is better)")
        axis.set_ylim(bottom=0)
        axis.set_ylim(top=axis.get_ylim()[1] * 1.2)
        axis.grid(axis="y", alpha=0.2)
        axis.set_axisbelow(True)
        axis.legend()
    fig.suptitle(title, fontsize=12)
    fig.text(0.5, 0.015, "Bars: repetition median | Error bars: sample standard deviation | Dots: repetition means (not per-order latencies)",
             ha="center", fontsize=9)
    fig.tight_layout(rect=(0, 0.06, 1, 0.91))
    for extension in ("png", "svg"):
        fig.savefig(directory / ("latency." + extension), dpi=170)
    plt.close(fig)

    cases = [(w, n) for w in workloads for n in counts if any((w, b, n) in lookup for b in BOOKS)]
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.1))
    for axis, metric, ylabel in zip(axes,
            ("heap_allocations_per_event", "heap_requested_bytes_per_event"),
            ("Heap allocation calls/event", "Heap requested bytes/event")):
        for book_index, (book, color) in enumerate(zip(BOOKS, COLORS)):
            positions, values = [], []
            for index, (workload, count) in enumerate(cases):
                if (workload, book, count) in lookup:
                    positions.append(index + (-0.2 if book_index == 0 else 0.2))
                    values.append(lookup[(workload, book, count)][metric])
            axis.bar(positions, values, width=0.36, color=color,
                     label="Heap" if book == "HeapOrderBook" else "Pool")
        short_labels = {"FillClearFirstUse": "First use", "FillClearReuse": "Reuse", "AddCancelChurn": "Churn"}
        axis.set_xticks(range(len(cases)), [f"{short_labels[w]}\n{n}" for w, n in cases], fontsize=9)
        axis.set_xlabel("Workload / active orders")
        axis.set_ylabel(ylabel + " (lower is better)")
        axis.set_ylim(bottom=0)
        axis.grid(axis="y", alpha=0.2)
        axis.set_axisbelow(True)
        axis.legend()
    fig.suptitle("Untimed allocation-count pass | " + commit)
    fig.text(0.5, 0.01, "Requested bytes are allocation traffic, not RSS or retained memory; book construction is excluded.", ha="center", fontsize=9)
    fig.tight_layout(rect=(0, 0.05, 1, 0.93))
    for extension in ("png", "svg"):
        fig.savefig(directory / ("allocations." + extension), dpi=170)
    plt.close(fig)
    write_csv(directory / "repetitions.csv", rows)
    write_csv(directory / "summary.csv", summaries)
    headers = ["Workload", "Orders", "Heap ns/event", "Pool ns/event", "Pool reduction", "Heap CV", "Pool CV"]
    table = []
    for workload, count in cases:
        heap = lookup.get((workload, "HeapOrderBook", count))
        pool = lookup.get((workload, "OrderBook", count))
        reduction = (1 - pool["median_cpu_ns_per_event"] / heap["median_cpu_ns_per_event"]) * 100 if heap and pool and heap["median_cpu_ns_per_event"] else None
        table.append([LABELS[workload], str(count),
                      f"{heap['median_cpu_ns_per_event']:.2f}" if heap else "—",
                      f"{pool['median_cpu_ns_per_event']:.2f}" if pool else "—",
                      f"{reduction:+.1f}%" if reduction is not None else "—",
                      f"{heap['cpu_cv_percent']:.1f}%" if heap else "—",
                      f"{pool['cpu_cv_percent']:.1f}%" if pool else "—"])
    notes = [
        "Each value is median CPU ns/event across repetition-level batch averages, not a per-order latency percentile.",
        "Fill/clear batches have 2N events; Add/Cancel churn batches have 8N events. Positive pool reduction means lower CPU cost.",
        "Book construction, seed/warm-up work, correctness checks and allocation counting are outside timing. Inactive allocation-probe overhead is included.",
        "Only core Add/Cancel is measured; Execute, registry dispatch, CSV parsing and logging are excluded.",
        "The frozen heap implementation and pooled implementation also differ in allocation-failure rollback logic.",
        "CPU scaling/background applications can affect results. Keep machine, affinity, compiler, library version and protocol identical when comparing runs.",
    ]
    markdown = ["# Benchmark report — " + directory.name, "", "Commit: `" + source.get("commit", "unknown") + "`" + dirty, "", environment, "",
                "| " + " | ".join(headers) + " |", "|---|---:|---:|---:|---:|---:|---:|"]
    markdown += ["| " + " | ".join(row) + " |" for row in table]
    markdown += ["", "![CPU cost](latency.png)", "", "![Allocation traffic](allocations.png)", ""]
    markdown += ["- " + note for note in notes]
    markdown += ["", "Raw repetitions: `benchmark.json`, `repetitions.csv`. Machine/build/protocol: `metadata.json`. All results are local artifacts under the ignored results directory.", ""]
    (directory / "summary.md").write_text("\n".join(markdown))
    esc = html.escape
    html_table = "<table><thead><tr>" + "".join("<th>" + esc(h) + "</th>" for h in headers) + "</tr></thead><tbody>"
    html_table += "".join("<tr>" + "".join("<td>" + esc(cell) + "</td>" for cell in row) + "</tr>" for row in table) + "</tbody></table>"
    page = """<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Order book benchmark report</title><style>body{font:16px system-ui;max-width:1400px;margin:32px auto;padding:0 24px;color:#172033;background:#f8fafc}h1{font-size:26px}img{width:100%;background:white;border-radius:8px;margin:20px 0}table{border-collapse:collapse;width:100%;background:white}td,th{padding:12px;text-align:right;border-bottom:1px solid #ddd}td:first-child,th:first-child{text-align:left}.scroll{overflow-x:auto}li{margin:10px 0}a{color:#0369a1}</style>"""
    page += "<h1>Benchmark report</h1><p>" + esc(directory.name) + " · Commit " + esc(commit + dirty) + "</p><p>" + esc(environment) + "</p>"
    page += '<div class="scroll">' + html_table + '</div><img src="latency.svg" alt="CPU cost by workload and implementation"><img src="allocations.svg" alt="Heap allocation traffic">'
    page += "<ul>" + "".join("<li>" + esc(note) + "</li>" for note in notes) + "</ul>"
    page += '<p><a href="benchmark.json">Original JSON</a> · <a href="repetitions.csv">Repetitions CSV</a> · <a href="summary.csv">Summary CSV</a> · <a href="metadata.json">Metadata</a></p></html>'
    (directory / "report.html").write_text(page)
    print("Report: " + str(directory / "report.html"))
    print("Summary: " + str(directory / "summary.md"))


def snapshot():
    return {"time": now(), "load_average_1_5_15": os.getloadavg(),
            "thermal_zones_millidegrees": {str(p): read_optional(p) for p in Path("/sys/class/thermal").glob("thermal_zone*/temp")}}


def run_benchmark(args):
    pyplot()  # Check the chart dependency before spending time building/measuring.
    commit = capture("git", "rev-parse", "HEAD")
    status = capture("git", "status", "--porcelain=v1")
    run_id = datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-" + commit[:12]
    directory = args.output_root.resolve() / run_id
    directory.mkdir(parents=True, exist_ok=False)
    metadata = {"run_id": run_id, "status": "building", "started_at": now(),
                "source": {"commit": commit, "tree": capture("git", "rev-parse", "HEAD^{tree}"),
                           "branch": capture("git", "branch", "--show-current"),
                           "working_tree_clean": not status, "status_porcelain": status},
                "system": {"uname": platform.uname()._asdict(), "cpu": json.loads(capture("lscpu", "--json")),
                           "available_affinity": sorted(os.sched_getaffinity(0)), "measurement_cpu": args.cpu,
                           "governors": {str(p): read_optional(p) for p in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_governor")}},
                "protocol": {"preset": args.preset, "minimum_time": args.min_time, "repetitions": args.repetitions,
                             "random_interleaving": True, "logging_enabled": False,
                             "scope": "Core Add/Cancel only; 128 and 256 live orders; deterministic order-id workload; no RNG seed",
                             "fill_clear_events": "2 * active_orders", "churn_events": "8 * active_orders"}}
    if status:
        (directory / "source.patch").write_text(capture("git", "diff", "HEAD", "--binary") + "\n")
        # Include untracked project sources as well as the tracked patch.
        for name in capture("git", "ls-files", "--others", "--exclude-standard", "--", "tools", "include", "src", "benchmarks", "tests", "cmake", "CMakeLists.txt").splitlines():
            path = ROOT / name
            if path.is_file():
                destination = directory / "untracked-source" / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
    def update():
        save_json(directory / "metadata.json", metadata)
    update()
    try:
        with (directory / "build.log").open("w") as log:
            subprocess.run(["cmake", "--preset", args.preset], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(["cmake", "--build", "--preset", args.preset, "--parallel", str(args.jobs)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        build = ROOT / "build" / args.preset
        for filename in ("CMakeCache.txt", "compile_commands.json"):
            shutil.copyfile(build / filename, directory / filename)
        binary = build / "order_book_allocator_benchmarks"
        cache = (build / "CMakeCache.txt").read_text()
        compiler_match = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", cache, re.MULTILINE)
        package_match = re.search(r"^benchmark_DIR:PATH=(.+)$", cache, re.MULTILINE)
        version_text = read_optional(Path(package_match.group(1)) / "benchmarkConfigVersion.cmake") if package_match else None
        version_match = re.search(r'set\(PACKAGE_VERSION "([^"]+)"\)', version_text or "")
        metadata["build"] = {"google_benchmark_package_version": version_match.group(1) if version_match else None,
                              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                              "compiler": capture(compiler_match.group(1), "--version") if compiler_match else "See CMakeCache.txt",
                              "cmake": capture("cmake", "--version"),
                              "configuration": "CMakeCache.txt", "compiler_commands": "compile_commands.json"}
        command = [str(binary), "--benchmark_min_time=" + args.min_time,
                   "--benchmark_repetitions=" + str(args.repetitions),
                   "--benchmark_enable_random_interleaving=true", "--benchmark_report_aggregates_only=false",
                   "--benchmark_display_aggregates_only=true", "--benchmark_color=false",
                   "--benchmark_out=" + str(directory / "benchmark.json"), "--benchmark_out_format=json"]
        if args.cpu is not None:
            if args.cpu not in os.sched_getaffinity(0):
                raise ValueError("Selected CPU is not in the available affinity set")
            command = ["taskset", "-c", str(args.cpu)] + command
        metadata["command"] = command
        (directory / "command.txt").write_text("cd " + shlex.quote(str(ROOT)) + "\n" + shlex.join(command) + "\n")
        metadata["status"] = "running"
        metadata["before"] = snapshot()
        update()
        print("Measuring: " + str(directory), flush=True)
        with (directory / "console.txt").open("w") as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True,
                           env=dict(os.environ, LC_ALL="C"))
        metadata["after"] = snapshot()
        metadata["benchmark_context"] = json.loads((directory / "benchmark.json").read_text()).get("context", {})
        render(directory)
        metadata["status"] = "completed"
    except (subprocess.CalledProcessError, ValueError, OSError):
        metadata["status"] = "failed"
        raise
    finally:
        metadata["finished_at"] = now()
        metadata["source"]["unchanged_after_run"] = (capture("git", "rev-parse", "HEAD") == commit and capture("git", "status", "--porcelain=v1") == status)
        update()
        print("Artifacts: " + str(directory), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="action", required=True)
    run = subcommands.add_parser("run", help="Build, measure and generate a report in a new directory")
    run.add_argument("--preset", default="release", choices=("release", "debug", "asan", "tsan"))
    run.add_argument("--output-root", type=Path, default=Path("results"))
    run.add_argument("--min-time", default="2s")
    run.add_argument("--repetitions", type=int, default=10)
    run.add_argument("--cpu", type=int, help="Pin benchmark to one allowed logical CPU")
    run.add_argument("--jobs", type=int, default=4)
    report = subcommands.add_parser("render", help="Generate charts from an existing benchmark.json")
    report.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.action == "run":
        if args.repetitions < 1 or args.jobs < 1 or not re.fullmatch(r"(?:\d+(?:\.\d*)?|\.\d+)s", args.min_time) or float(args.min_time[:-1]) <= 0:
            parser.error("repetitions, jobs and minimum seconds must be positive")
        run_benchmark(args)
    else:
        render(args.directory)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError) as error:
        print("Benchmark report failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)
