import importlib.util
from pathlib import Path
import unittest

MODULE_PATH = Path(__file__).resolve().parents[2] / 'tools' / 'benchmark_report.py'
spec = importlib.util.spec_from_file_location('benchmark_report', MODULE_PATH)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def sample(workload='FillClearFirstUse', cpu=25.6, index=0, unit='us'):
    return {'name': f'BM_{workload}<OrderBook>/128', 'run_type': 'iteration',
            'cpu_time': cpu, 'real_time': cpu * 2, 'time_unit': unit,
            'repetition_index': index, 'iterations': 100,
            'heap_allocs/event': 0.5, 'heap_requested_bytes/event': 16}


class BenchmarkReportTests(unittest.TestCase):
    def test_normalizes_fill_clear_and_churn_by_different_event_counts(self):
        rows, _, _ = report.parse_results({'benchmarks': [sample(), sample('AddCancelChurn', 102.4)]})
        self.assertEqual([r['events_per_batch'] for r in rows], [256, 1024])
        for row in rows:
            self.assertAlmostEqual(row['cpu_ns_per_event'], 100)
            self.assertAlmostEqual(row['wall_ns_per_event'], 200)

    def test_aggregates_are_excluded_from_repetition_statistics(self):
        aggregate = dict(sample(cpu=999), run_type='aggregate', aggregate_name='median')
        rows, summaries, _ = report.parse_results({'benchmarks': [sample(cpu=20.48), sample(cpu=30.72, index=1), aggregate]})
        self.assertEqual(len(rows), 2)
        self.assertEqual(summaries[0]['repetitions'], 2)
        self.assertAlmostEqual(summaries[0]['median_cpu_ns_per_event'], 100)
        self.assertAlmostEqual(summaries[0]['mean_cpu_ns_per_event'], 100)
        self.assertAlmostEqual(summaries[0]['cpu_cv_percent'], 28.2842712474619)

    def test_accepts_other_google_benchmark_time_units(self):
        for unit, cpu in [('ns', 25600), ('us', 25.6), ('ms', 0.0256), ('s', 0.0000256)]:
            rows, _, _ = report.parse_results({'benchmarks': [sample(cpu=cpu, unit=unit)]})
            self.assertAlmostEqual(rows[0]['cpu_ns_per_event'], 100)

    def test_rejects_benchmark_errors_instead_of_plotting_success(self):
        with self.assertRaisesRegex(ValueError, 'pool exhausted'):
            report.parse_results({'benchmarks': [dict(sample(), error_occurred=True, error_message='pool exhausted')]})

    def test_rejects_aggregate_only_input(self):
        with self.assertRaisesRegex(ValueError, 'No individual repetitions'):
            report.parse_results({'benchmarks': [dict(sample(), run_type='aggregate')]})

    def test_rejects_unknown_workload_without_guessing_event_count(self):
        with self.assertRaisesRegex(ValueError, 'Cannot determine event count'):
            report.parse_results({'benchmarks': [dict(sample(), name='BM_Other/128')]})


if __name__ == '__main__':
    unittest.main()
