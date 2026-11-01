"""Report delivery contracts: offline HTML, safe embedding, concise Markdown."""
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('feed_report',
    Path(__file__).resolve().parents[2] / 'tools/feed_report.py')
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


def run(mode='on'):
    result = dict(records=7, book_updates=4, elapsed_seconds=1,
                  messages_per_second=7, complete_input=True, limited=False,
                  session_ended=True, invariants_valid=True, mode=mode, repetition=1)
    if mode == 'on':
        stats = dict(p50_ns=10, p99_ns=20, p999_ns=30)
        result['latency'] = dict(read_decode_apply=stats, decode_apply=stats,
                                 selected_update_decode_apply=stats, clock_pair_overhead=stats)
    return result


class FeedReportTests(unittest.TestCase):
    def test_offline_safe_html_and_exact_64bit_order_ids(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            hostile = '</script><img src=x onerror=alert(1)>'
            diagnostic = dict(records=[dict(order_id=2**64-1, wall_ns=100)])
            REPORT.write_report(root, [run('off'), run()], metadata=dict(symbol=hostile),
                                diagnostic=diagnostic, findings=[hostile])
            page = (root / 'report.html').read_text()
            self.assertEqual(page.count('</script>'), 2)
            self.assertNotIn(hostile, page)
            self.assertNotRegex(page, r'<script[^>]+src=')
            self.assertNotIn('<link ', page)
            data = json.loads(re.search(r'id="report-data">(.*?)</script>',page,re.S)[1])
            self.assertEqual(data['diagnostic']['records'][0]['order_id'], str(2**64-1))
            self.assertEqual(diagnostic['records'][0]['order_id'], 2**64-1)
            self.assertEqual(data['findings'], [hostile])
            markdown = (root / 'report.md').read_text()
            self.assertIn('(report.html)', markdown)
            self.assertLess(len(markdown.splitlines()), 12)

    def test_partial_control_report_does_not_require_latency_or_resources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            REPORT.write_report(root, [run('off')], metadata=dict(repetitions=2,
                                                                compare_instrumentation=True))
            page = (root / 'report.html').read_text()
            data = json.loads(re.search(r'id="report-data">(.*?)</script>',page,re.S)[1])
            self.assertEqual(len(data['runs']), 1)
            self.assertNotIn('latency', data['runs'][0])
            self.assertIn('Đang đo / chưa đủ kiểm chứng', page)

    def test_pipeline_report_keeps_network_and_journal_semantics(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stats = dict(p50_ns=1000, p99_ns=2000, p999_ns=3000)
            pipeline = dict(directory='<bad>', journal='off', checks=dict(valid=True),
                            latency=dict(commands_per_second=100,
                                         complete=stats, market_applied=stats))
            REPORT.write_pipeline_report(root, [pipeline])
            page = (root / 'report.html').read_text()
            self.assertIn('LOCAL TCP / UDP', page)
            self.assertIn('1.00 / 2.00 / 3.00', page)
            self.assertIn('&lt;bad&gt;', page)
            self.assertNotIn('<bad>', page)
            self.assertIn('(report.html)', (root / 'report.md').read_text())


if __name__ == '__main__':
    unittest.main()
