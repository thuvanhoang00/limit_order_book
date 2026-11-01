import json
from pathlib import Path
import statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root=Path(__file__).resolve().parent
runs=json.loads((root/'summary.json').read_text())
fig,(ax,throughput)=plt.subplots(1,2,figsize=(12,4.6),gridspec_kw={'width_ratios':[2.3,1]})
paths=[('read_decode_apply','Read + decode/apply','#1976d2'),
       ('decode_apply','Decode/apply, all records','#687687'),
       ('selected_update_decode_apply','Decode/apply, AAPL updates','#e27b21')]
keys=['p50_ns','p99_ns','p999_ns'];width=.24
for group,(path,label,color) in enumerate(paths):
    positions=[i+(group-1)*width for i in range(3)]
    medians=[statistics.median(r['latency'][path][key] for r in runs)/1000 for key in keys]
    ax.bar(positions,medians,width*.9,color=color,alpha=.8,label=label)
    for repetition,run in enumerate(runs):
        ax.scatter([x+(repetition-1)*.045 for x in positions],
                   [run['latency'][path][key]/1000 for key in keys],color='black',s=12,zorder=4)
ax.set_xticks(range(3),['p50','p99','p99.9']);ax.set_ylabel('Offline service time (µs)')
ax.set_title('368,366,634 ITCH messages per run; AAPL selected')
ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True);ax.legend(fontsize=8)
rates=[r['messages_per_second']/1e6 for r in runs]
throughput.bar(range(1,4),rates,color='#1976d2',alpha=.8)
throughput.axhline(statistics.median(rates),color='black',linestyle='--',linewidth=1)
throughput.set_xticks(range(1,4),['Run 1','Run 2','Run 3'])
throughput.set_ylabel('Million messages/s');throughput.set_title('Instrumented throughput')
throughput.grid(axis='y',alpha=.2);throughput.set_axisbelow(True)
fig.suptitle('Nasdaq ITCH 2019-01-30 — complete archive, CPU 2, powersave',fontsize=12)
fig.text(.02,.01,'Bars: medians across per-run latency statistics. Dots: individual runs. Buffered gzip; no network or matching-engine latency.',fontsize=8)
fig.tight_layout(rect=(0,.04,1,.94))
fig.savefig(root/'latency-throughput.png',dpi=180)
fig.savefig(root/'latency-throughput.svg')
