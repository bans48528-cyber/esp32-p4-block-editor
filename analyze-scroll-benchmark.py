import re,json,sys
from pathlib import Path

def read(path):
    rows=[]; active=False
    for line in Path(path).read_text(encoding='utf-8',errors='replace').splitlines():
        if 'scroll_bench: BEGIN' in line: active=True; continue
        if 'scroll_bench: END' in line: active=False
        if not active or 'scroll_perf:' not in line: continue
        vals=dict(re.findall(r'(\w+)=([\d.]+)',line))
        if vals and float(vals.get('frames',0))==float(vals.get('scroll',-1)):
            rows.append({k:float(v) for k,v in vals.items()})
    if not rows:return {'file':path,'windows':0}
    n=sum(r['frames'] for r in rows);ms=sum(r['window'] for r in rows)
    out={'file':path,'windows':len(rows),'frames':n,'fps':round(n*1000/ms,2),'minimum_window_fps':round(min(r['frames']*1000/r['window'] for r in rows),2)}
    for k in ['avg','work','flush','ppa','lcd','wait','pixels','batches']:
        out[k]=round(sum(r[k]*r['frames'] for r in rows)/n,2)
    out['max_ms']=max(r['max'] for r in rows)
    out['cached_ratio']=round(sum(r['cached'] for r in rows)/n,3)
    return out
print(json.dumps([read(p) for p in sys.argv[1:]],ensure_ascii=False,indent=2))
