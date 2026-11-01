"""Offline HTML presentation for feed measurements; raw JSON remains authoritative."""
import json
import html
from pathlib import Path
import statistics


def write_report(directory, runs, metadata=None, diagnostic=None, hotspots=None, findings=None):
    directory = Path(directory)
    if metadata is None:
        path = directory / 'metadata.json'
        metadata = json.loads(path.read_text()) if path.exists() else {}
    if diagnostic is not None:
        diagnostic = dict(diagnostic, records=[dict(r, order_id=str(r['order_id']))
                                               for r in diagnostic.get('records', [])])
    payload = dict(runs=runs, metadata=metadata, diagnostic=diagnostic, hotspots=hotspots or [],
                   findings=findings or [])
    # Prevent archive/configuration strings from ending the data element.
    data = json.dumps(payload, ensure_ascii=False).replace('<', '\\u003c')
    (directory / 'report.html').write_text(_HTML.replace('__REPORT_DATA__', data))
    on = [r for r in runs if 'latency' in r]
    controls = [r for r in runs if r.get('mode') == 'off']
    rate = statistics.median(r['messages_per_second'] for r in (controls or runs)) if runs else 0
    lines = ['# Feed measurement', '', '[Open the standalone HTML report](report.html).', '',
             f'{len(runs)} completed runs; median {"observer-off" if controls else "recorded"} throughput {rate / 1e6:.3f} million messages/s.',
             'Raw results and measurement metadata: `summary.json`, `metadata.json`.',
             'Offline replay service times; includes observer overhead when enabled.']
    if on:
        p99 = statistics.median(r['latency']['selected_update_decode_apply']['p99_ns'] for r in on)
        lines.append(f'Selected-book decode/apply p99: {p99 / 1000:.3f} µs (median per-run statistic).')
    (directory / 'report.md').write_text('\n'.join(lines) + '\n')


def write_pipeline_report(directory, runs):
    """The TCP/UDP workload has different semantics from historical feed replay."""
    directory = Path(directory)
    escaped = lambda value: html.escape(str(value), quote=True)
    rows = []
    bars = []
    peak = max((r['latency']['commands_per_second'] for r in runs), default=1)
    for run in runs:
        latency = run['latency']
        values = [run['directory'], run['journal'], f'{latency["commands_per_second"]:.1f}']
        for path in ('complete', 'market_applied'):
            values.append(' / '.join(f'{latency[path][p] / 1000:.2f}'
                                    for p in ('p50_ns', 'p99_ns', 'p999_ns')))
        values.append('PASS' if all(run['checks'].values()) else 'FAIL')
        rows.append('<tr>' + ''.join('<td>' + escaped(v) + '</td>' for v in values) + '</tr>')
        width = latency['commands_per_second'] / peak * 100 if peak else 0
        bars.append(f'<div class="row"><label>{escaped(run["directory"])}</label><div class="track">'
                    f'<div class="bar" style="width:{width:.3f}%"></div></div>'
                    f'<span class="value">{latency["commands_per_second"]:.1f}/s</span></div>')
    headers = ['Run', 'Journal', 'Commands/s', 'Complete p50 / p99 / p99.9 (µs)',
               'Market applied p50 / p99 / p99.9 (µs)', 'Checks']
    style = _HTML.split('<style>', 1)[1].split('</style>', 1)[0]
    report = ('<!doctype html><html lang="vi"><head><meta charset="utf-8">'
              '<meta name="viewport" content="width=device-width,initial-scale=1">'
              '<title>Local trading pipeline</title><style>' + style + '</style></head><body><main>'
              '<span class="tag">LOCAL TCP / UDP</span><h1>Trading pipeline benchmark</h1>'
              '<section><h2>Phạm vi đo</h2><p>Một lệnh đang chờ trên toàn bộ hai TradingClient. '
              'Đợi Complete và market watermark trước lệnh tiếp theo. Throughput gồm kiểm tra report '
              'giữa các lệnh; chưa đo công suất bão hòa. Market applied kết thúc sau Complete và '
              'áp dụng đủ UDP deltas vào local book. Journal bật giữ fdatasync trước xử lý.</p></section>'
              '<section><h2>Throughput theo lượt</h2>' + ''.join(bars) + '</section>'
              '<section><h2>Latency và kiểm chứng</h2><div class="scroll"><table><thead><tr>' +
              ''.join('<th>' + escaped(v) + '</th>' for v in headers) + '</tr></thead><tbody>' +
              ''.join(rows) + '</tbody></table></div></section>'
              '<section><details><summary>Dữ liệu gốc và điều kiện đo</summary>'
              '<p><a href="summary.json">Summary JSON</a> · <a href="metadata.json">Metadata</a></p>'
              '<pre>' + escaped(json.dumps(runs, indent=2)) + '</pre></details></section>'
              '<p>Standalone HTML, không tải nội dung từ mạng.</p></main></body></html>')
    (directory / 'report.html').write_text(report)
    (directory / 'report.md').write_text('# Local trading pipeline\n\n'
        '[Open the standalone HTML report](report.html).\n\n'
        f'{len(runs)} completed runs. Raw checks/results: `summary.json`; conditions: `metadata.json`.\n'
        'Closed-loop achieved throughput; Complete and local market application have separate timers.\n')


_HTML = r'''<!doctype html>
<html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LOB — báo cáo đo hiệu năng</title>
<style>
:root{font-family:system-ui,sans-serif;color:#182638;background:#f3f6fa;font-size:15px}
*{box-sizing:border-box}body{margin:0}main{max-width:1260px;margin:auto;padding:30px 22px}
h1{font-size:30px;margin:5px 0 12px}h2{font-size:20px;margin:0 0 14px}h3{font-size:16px}
p{line-height:1.6}small,.muted{color:#59697b}a{color:#1553aa}.tag{display:inline-block;background:#dce9fb;color:#184785;border-radius:14px;padding:5px 11px;margin:2px}
.ok{background:#d8f0e3;color:#146341}.pending{background:#fff0cb;color:#77540b}
.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin:22px 0}
.card,section{background:white;border:1px solid #dce3eb;border-radius:12px;padding:19px;margin-bottom:18px}
.card{margin:0}.number{font-size:28px;font-weight:700;margin:10px 0}.label{font-size:13px;color:#526174}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.grid section{min-width:0}
.scroll{overflow:auto;max-height:530px}table{border-collapse:collapse;width:100%;font-size:13px}
th,td{padding:10px;text-align:right;border-bottom:1px solid #e6ebf0;white-space:nowrap}
th{position:sticky;top:0;background:#edf3fa;color:#304b6b;cursor:pointer}td:first-child,th:first-child{text-align:left}
input,select{padding:9px;border:1px solid #c7d3e1;border-radius:6px;margin:0 8px 12px 0}
details{margin:14px 0}summary{cursor:pointer;font-weight:600}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;background:#f5f7fa;padding:12px}
.row{display:flex;align-items:center;gap:8px;margin:13px 0}.row label{width:105px;font-size:12px;flex:none;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.track{flex:1;background:#edf2f8;border-radius:4px;min-width:20px}.bar{height:18px;background:#3275c8;border-radius:4px}
.orange{background:#d47a2f}.value{width:95px;font-size:12px;text-align:right;flex:none}
.legend{font-size:12px;margin:10px 0;color:#526174}.dot{display:inline-block;width:10px;height:10px;background:#3275c8;margin-right:5px}.dot.orange{background:#d47a2f}
li{line-height:1.65;margin:5px 0}.wide td:last-child{white-space:normal;text-align:left;min-width:260px;overflow-wrap:anywhere}
@media(max-width:800px){.cards{grid-template-columns:repeat(2,1fr)}.grid{grid-template-columns:1fr}h1{font-size:25px}}
@media print{body{background:white}.scroll{max-height:none}details{display:block}.cards{break-inside:avoid}section{break-inside:avoid}}
</style></head><body><main>
<header><span class="tag">OFFLINE REPLAY</span><span class="tag" id="source"></span><span class="tag" id="status"></span>
<h1>Baseline để quyết định tối ưu</h1><p class="muted" id="scope"></p></header>
<div class="cards" id="cards"></div>
<section><h2>Kết luận và việc tiếp theo</h2><ul id="findings"></ul></section>
<div class="grid"><section><h2>Bật / tắt instrumentation</h2><div class="legend"><i class="dot"></i>Tắt đo <i class="dot orange"></i>Bật đo</div>
<div id="rates"></div><p class="muted">Cùng binary và dữ liệu. Đây là chi phí bật observer, không phải speedup của implementation.</p></section>
<section><h2>Latency theo đường xử lý</h2><select id="percentile"><option value="p50_ns">p50</option><option value="p99_ns" selected>p99</option><option value="p999_ns">p99.9</option></select>
<div id="latencies"></div><p class="muted">Median của thống kê từng lượt. Đọc/gzip có buffer; đồng hồ và histogram có overhead.</p></section></div>
<section><h2>Số đo từng lượt</h2><div class="scroll"><table id="runs"></table></div>
<details><summary>Đủ percentile, mean và max theo từng đường xử lý</summary><div class="scroll"><table id="distributions"></table></div></details>
<details><summary>Overhead từng cặp và định nghĩa</summary><div class="scroll"><table id="pairs"></table></div>
<p>Time overhead = on / off − 1. Throughput loss = 1 − rate_on / rate_off. Không trừ clock overhead khỏi percentile.</p>
<p>Peak RSS và CPU time là số liệu toàn process từ GNU time; elapsed replay không gồm kiểm tra và xuất report cuối process.</p></details></section>
<div class="grid"><section><h2>CPU hotspots</h2><p id="perf-note" class="muted"></p><div id="hotspots"></div><details><summary>CPU samples chi tiết</summary><pre id="cpu-detail"></pre></details></section>
<section><h2>Record chậm: CPU hay ngoài CPU?</h2><div id="diagnostic"></div></section></div>
<section><h2>Chi tiết outlier</h2><p class="muted" id="retention"></p>
<input id="filter" placeholder="Tìm record, type, order ID"><select id="cause"><option value="all">Tất cả</option><option value="switch">Có context switch</option><option value="fault">Có page fault</option><option value="cpu">CPU ≥ 80% wall</option></select>
<div class="scroll"><table id="outliers"></table></div>
<details><summary>Cách đọc chẩn đoán</summary><p>CPU/resource được quan sát trên cửa sổ hơi rộng hơn wall time. Wall − CPU được chặn ở 0; context switch/fault là bằng chứng đi kèm, không phải scheduler trace chính xác. Lượt perf/outlier chạy riêng và không dùng để tính overhead bật histogram.</p></details></section>
<section><details><summary>Điều kiện đo, phạm vi và dữ liệu gốc</summary>
<p>Không đo RTT mạng, queue, journal hay order-entry matching; không tái hiện tốc độ đến của dữ liệu lịch sử. Chạy local với governor hiện có, không flush cache. Xem metadata để biết chính xác binary, nguồn, tham số và CPU.</p>
<p><a href="summary.json">Kết quả JSON</a> · <a href="metadata.json">Metadata JSON</a> · <a href="commands.json">Lệnh đã chạy</a></p><pre id="metadata"></pre></details></section>
<footer class="muted">HTML độc lập, không tải thư viện hay dữ liệu từ mạng. JSON và histogram là nguồn số liệu gốc.</footer>
</main><script type="application/json" id="report-data">__REPORT_DATA__</script>
<script>
const data=JSON.parse(document.getElementById('report-data').textContent), runs=data.runs, meta=data.metadata;
const byId=id=>document.getElementById(id), fmt=(x,n=3)=>x==null?'—':Number(x).toLocaleString('vi-VN',{maximumFractionDigits:n});
const median=a=>{a=a.filter(x=>x!=null).sort((a,b)=>a-b);return a.length?(a[Math.floor(a.length/2)]+a[Math.floor((a.length-1)/2)])/2:null};
const mode=r=>r.mode||(r.latency?'on':'off'), on=runs.filter(r=>mode(r)==='on'), off=runs.filter(r=>mode(r)==='off');
function text(id,value){byId(id).textContent=value}
function table(id,headers,rows){const el=byId(id);el.replaceChildren();const head=el.createTHead().insertRow();headers.forEach((h,i)=>{const th=document.createElement('th');th.textContent=h;th.onclick=()=>{rows.sort((a,b)=>typeof a[i]==='number'?b[i]-a[i]:String(a[i]).localeCompare(String(b[i])));table(id,headers,rows)};head.append(th)});const body=el.createTBody();rows.forEach(row=>{const tr=body.insertRow();row.forEach(value=>{tr.insertCell().textContent=typeof value==='number'?fmt(value):value})})}
const pairIDs=[...new Set(runs.map(r=>r.repetition).filter(x=>x!=null))],pairs=[];
pairIDs.forEach(i=>{const a=runs.find(r=>r.repetition===i&&mode(r)==='off'),b=runs.find(r=>r.repetition===i&&mode(r)==='on');if(a&&b)pairs.push([i,a.elapsed_seconds,b.elapsed_seconds,(b.elapsed_seconds/a.elapsed_seconds-1)*100,(1-b.messages_per_second/a.messages_per_second)*100])});
const checks=runs.length&&runs.every(r=>r.complete_input&&!r.limited&&r.session_ended&&r.invariants_valid),expected=(meta.repetitions||runs.length)*(meta.compare_instrumentation?2:1),complete=checks&&runs.length===expected;
text('source',meta.symbol||runs[0]?.symbol_filter||'');text('status',complete?'Replay checks PASS':'Đang đo / chưa đủ kiểm chứng');byId('status').classList.add(complete?'ok':'pending');
text('scope',`${fmt(runs[0]?.records,0)} record / lượt · ${runs.length}/${expected} lượt · CPU ${(meta.cpu_affinity||[]).join(', ')} · governor ${meta.scaling_governor||'không ghi nhận'}`);
function card(label,value,note){const el=document.createElement('div');el.className='card';const l=document.createElement('div');l.className='label';l.textContent=label;const v=document.createElement('div');v.className='number';v.textContent=value;const p=document.createElement('small');p.textContent=note;el.append(l,v,p);byId('cards').append(el)}
card(off.length?'Baseline tắt đo':'Throughput bật đo',fmt(median((off.length?off:on).map(r=>r.messages_per_second))/1e6)+' M/s','Median từng lượt');
const selectedP99=median(on.map(r=>r.latency.selected_update_decode_apply.p99_ns));
card('p99 cập nhật book',selectedP99==null?'—':fmt(selectedP99/1000)+' µs','Có decode/routing; không phải core apply riêng');
card('Time overhead',pairs.length?fmt(median(pairs.map(p=>p[3])),2)+'%':'—','Chỉ tính trên cặp đầy đủ');
card('Peak RAM',runs.some(r=>r.resources)?fmt(Math.max(0,...runs.map(r=>r.resources?.max_rss_kb||0))/1024,1)+' MiB':'—','Đỉnh RSS cao nhất trong các lượt có ghi nhận');
const findings=[...data.findings];
if(!findings.length){if(pairs.length)findings.push(`Observer: median time overhead ${fmt(median(pairs.map(p=>p[3])),2)}%; throughput loss ${fmt(median(pairs.map(p=>p[4])),2)}%.`);findings.push(complete?'Các lượt replay đã đạt EOF/CRC, end-of-session và invariants. Đây chưa phải chứng nhận production.':'Báo cáo hiện chưa đủ tất cả lượt/kiểm chứng; chưa chốt baseline.');if(!data.diagnostic)findings.push('Chưa có dữ liệu outlier/CPU sampling trong báo cáo này; chưa kết luận nguyên nhân spike.');findings.push('Giữ workload và điều kiện đo, chỉ tối ưu một bottleneck có bằng chứng rồi so sánh lại.');}
findings.forEach(f=>{const li=document.createElement('li');li.textContent=f;byId('findings').append(li)});
function bars(id,items,unit){const el=byId(id);el.replaceChildren();const max=Math.max(1e-12,...items.map(i=>i.value));items.forEach(i=>{const row=document.createElement('div');row.className='row';const label=document.createElement('label');label.textContent=i.label;label.title=i.label;const track=document.createElement('div');track.className='track';const bar=document.createElement('div');bar.className='bar'+(i.orange?' orange':'');bar.style.width=(i.value/max*100)+'%';track.append(bar);const value=document.createElement('div');value.className='value';value.textContent=fmt(i.value)+' '+unit;row.append(label,track,value);el.append(row)})}
bars('rates',runs.map((r,i)=>({label:`${r.repetition||i+1} · ${mode(r)}`,value:r.messages_per_second/1e6,orange:mode(r)==='on'})),'M/s');
function latencyChart(){const p=byId('percentile').value,items=[['read_decode_apply','Đọc + xử lý'],['decode_apply','Decode/apply'],['selected_update_decode_apply','Cập nhật book']].map(([k,label])=>({label,value:median(on.map(r=>r.latency[k][p]))})).filter(i=>i.value!=null).map(i=>({...i,value:i.value/1000}));if(items.length)bars('latencies',items,'µs');else text('latencies','Chưa có latency được đo.');}
latencyChart();byId('percentile').onchange=latencyChart;
table('runs',['Lượt','Chế độ','Replay s','M msg/s','Book updates','CPU user s','Peak RSS MiB'],runs.map((r,i)=>[r.repetition||i+1,mode(r),r.elapsed_seconds,r.messages_per_second/1e6,r.book_updates,r.resources?.user_seconds??'—',r.resources?r.resources.max_rss_kb/1024:'—']));
table('distributions',['Lượt','Đường xử lý','Count','p50 µs','p99 µs','p99.9 µs','Mean µs','Max µs'],on.flatMap((r,i)=>['read_decode_apply','decode_apply','selected_update_decode_apply'].map(k=>{const s=r.latency[k];return [r.repetition||i+1,k,s.count,...['p50_ns','p99_ns','p999_ns','mean_ns','max_ns'].map(p=>s[p]==null?'—':s[p]/1000)]})));
table('pairs',['Cặp','Off s','On s','Time overhead %','Throughput loss %'],pairs);
text('perf-note',data.hotspots.length?'CPU self samples (user space), thu ở lượt chẩn đoán riêng.':'Chưa có CPU sampling; không suy đoán hotspot từ latency.');
text('cpu-detail',JSON.stringify(data.hotspots,null,2));
bars('hotspots',data.hotspots.slice(0,10).map(h=>({label:h.label||h.symbol?.split('(')[0]||h.dso,value:h.percent})),'% mẫu');
const diag=data.diagnostic;
if(diag){text('retention',`${fmt(diag.retained,0)} record chậm nhất được giữ, trên ${fmt(diag.slow_records,0)} record ≥ ${fmt(diag.threshold_ns/1000)} µs. Các nhóm fault/context-switch có thể chồng nhau.`);const ul=document.createElement('ul');[['Quan sát',diag.observations],['Record chậm',diag.slow_records],['Có context switch',diag.slow_with_context_switch],['Ngoài CPU ≥ 50% và có switch',diag.slow_off_cpu_with_switch],['CPU ≥ 80% wall',diag.slow_cpu_dominant],['Có page fault',diag.slow_with_fault]].forEach(([label,n])=>{const li=document.createElement('li');li.textContent=label+': '+fmt(n,0);ul.append(li)});byId('diagnostic').append(ul)}else{text('diagnostic','Chưa cung cấp trace riêng.');text('retention','Không có dữ liệu outlier trong báo cáo này.')}
function outliers(){const term=byId('filter').value.toLowerCase(),cause=byId('cause').value;const rows=(diag?.records||[]).filter(r=>(`${r.record} ${r.type} ${r.order_id}`).toLowerCase().includes(term)).filter(r=>cause==='all'||cause==='switch'&&(r.voluntary_switches+r.involuntary_switches)>0||cause==='fault'&&(r.minor_faults+r.major_faults)>0||cause==='cpu'&&r.thread_cpu_ns>=r.wall_ns*.8);table('outliers',['Record','Type','Order ID','Wall µs','CPU µs','Ngoài CPU µs','Switch tự/ngắt','Fault nhỏ/lớn'],rows.map(r=>[r.record,r.type,String(r.order_id),r.wall_ns/1000,r.thread_cpu_ns/1000,r.estimated_off_cpu_ns/1000,`${r.voluntary_switches} / ${r.involuntary_switches}`,`${r.minor_faults} / ${r.major_faults}`]))}
outliers();byId('filter').oninput=outliers;byId('cause').onchange=outliers;text('metadata',JSON.stringify(meta,null,2));
</script></body></html>'''
