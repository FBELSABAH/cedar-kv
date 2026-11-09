import json,platform,statistics,tempfile,time
from pathlib import Path
from test import Server,command
def measure(f,commands,expected):
 samples=[];start=time.perf_counter()
 for cmd in commands:
  t=time.perf_counter_ns();reply=command(f,cmd);samples.append((time.perf_counter_ns()-t)/1e6);assert reply.startswith(expected),reply
 elapsed=time.perf_counter()-start;samples.sort()
 return {'operations':len(samples),'seconds':round(elapsed,4),'ops_per_second':round(len(samples)/elapsed,1),'p50_ms':round(statistics.median(samples),4),'p95_ms':round(samples[int(.95*(len(samples)-1))],4)}
with tempfile.TemporaryDirectory() as d:
 server=Server(Path(d)/'bench.log')
 try:
  s,f=server.connect()
  with s,f:
   result={'platform':platform.platform(),'method':'1000 sequential localhost requests per phase; SET includes fsync; client overhead included; one run, not production capacity', 'SET':measure(f,[f'SET k{i} value{i}' for i in range(1000)],'OK'),'GET':measure(f,[f'GET k{i}' for i in range(1000)],'VALUE')}
  Path('benchmark-results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
 finally:server.stop()
