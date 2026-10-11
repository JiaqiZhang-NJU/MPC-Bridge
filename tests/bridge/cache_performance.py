"""Ten identical byte-read trials: direct origin, cold bridge, warm bridge.

Measures HTTP byte delivery, not rendered first frame or native seek latency.
Results are local artifacts; no speedup assertion is used as a correctness test.
"""
import csv
import json
import statistics
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from cache_integration import ROOT, DATA, query, start, stop

class LimitedOrigin(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    count = 0
    bytes_sent = 0
    def log_message(self, *args): pass
    def do_GET(self):
        first, last = map(int, self.headers['Range'].removeprefix('bytes=').split('-'))
        type(self).count += 1
        time.sleep(.02)
        self.send_response(206)
        self.send_header('Content-Range', f'bytes {first}-{last}/{len(DATA)}')
        self.send_header('Content-Length', str(last-first+1))
        self.send_header('ETag', '"perf-v1"')
        self.send_header('Connection', 'close')
        self.end_headers()
        try:
            for offset in range(first,last+1,65536):
                data = DATA[offset:min(last+1,offset+65536)]
                time.sleep(len(data)/(2*1024*1024)) # deterministic 2 MiB/s per request
                self.wfile.write(data)
                type(self).bytes_sent += len(data)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError): pass

def main():
    server = ThreadingHTTPServer(('127.0.0.1',0),LimitedOrigin)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever,daemon=True).start()
    origin = f'http://127.0.0.1:{server.server_port}/media'
    process,route,folder = start(origin,'performance')
    rows=[]
    try:
        process.stdin.write('idle\n'); process.stdin.flush()
        assert process.stdout.readline().strip()=='IDLE'
        for trial in range(10):
            # All reads transfer the same exact 1 MiB chunk for a fair origin comparison.
            first=(trial+2)*1048576; last=first+1048575
            for condition,url in [('direct',origin),('cold',route),('warm',route)]:
                if condition=='cold':
                    process.stdin.write('clear\nstatus\n'); process.stdin.flush()
                    assert process.stdout.readline().startswith('STATUS ')
                requests_before=LimitedOrigin.count; bytes_before=LimitedOrigin.bytes_sent
                begin=time.perf_counter(); status,headers,body=query(url,f'bytes={first}-{last}')
                elapsed=(time.perf_counter()-begin)*1000
                assert status==206 and body==DATA[first:last+1]
                count=LimitedOrigin.count-requests_before
                if condition=='warm': assert count==0, 'warm cache contacted the origin'
                rows.append(dict(trial=trial+1,condition=condition,milliseconds=round(elapsed,3),origin_requests=count,origin_bytes=LimitedOrigin.bytes_sent-bytes_before))
        process.stdin.write('status\n'); process.stdin.flush()
        cache_status=process.stdout.readline().strip()
    finally:
        stop(process,folder); server.shutdown()
    output=ROOT/'_bin/bridge/cache-performance'; output.mkdir(parents=True,exist_ok=True)
    with (output/'trials.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.DictWriter(file,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    summary=dict(measurement='HTTP byte delivery; not native seek or first frame',trials_per_condition=10,rate_mib_per_second=2,request_delay_ms=20,
        median_ms={c:statistics.median(r['milliseconds'] for r in rows if r['condition']==c) for c in ('direct','cold','warm')},
        warm_origin_requests=sum(r['origin_requests'] for r in rows if r['condition']=='warm'),cache_status=cache_status)
    (output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False))

if __name__=='__main__': main()
