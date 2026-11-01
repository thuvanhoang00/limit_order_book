"""Rebuild the HTML presentation from archived JSON without replaying the feed."""
import json
from pathlib import Path
import sys

directory = Path(__file__).resolve().parent
sys.path.insert(0, str(directory.parents[2] / 'tools'))
from feed_report import write_report

read = lambda name: json.loads((directory / name).read_text())
metadata = dict(read('metadata.json'), presentation=read('presentation.json'))
write_report(directory, read('summary.json'), metadata=metadata,
             diagnostic=read('diagnostic/outliers.json'),
             hotspots=read('diagnostic/cpu-self.json'), findings=read('findings.json'))
