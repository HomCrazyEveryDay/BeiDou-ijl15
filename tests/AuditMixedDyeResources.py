from pathlib import Path
import argparse, subprocess, concurrent.futures, json
parser=argparse.ArgumentParser(description='Audit local appearance families with the production native composer; never opens the game.')
parser.add_argument('--probe',required=True,type=Path,help='Compiled MixedDyeTest.exe (build via MixedDyeTest.ps1)')
parser.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[2])
parser.add_argument('--output',type=Path,help='Separate evidence directory for this audit')
parser.add_argument('--workers',type=int,default=2,choices=range(1,9))
args=parser.parse_args();root=args.workspace.resolve();client=root/'BeiDou-Client';exe=args.probe.resolve()
out=args.output.resolve() if args.output else root/'exports/mixed-dye-audit';out.mkdir(parents=True,exist_ok=True)
groups={}
for kind in ['Hair','Face']:
 for p in (client/'Data/Character'/kind).glob('*.img'):
  try: n=int(p.stem)
  except ValueError: continue
  color=n//100%10 if kind=='Face' else n%10
  # Match the explicit v83/extended style classifier, not just a filename.
  face=20000<=n<30000 or 50000<=n<60000 or 80000<=n<90000 or n in (40902,40991) or 42150<=n<=42157 or 42160<=n<=42167
  hair=not face and (30000<=n<50000 or 60000<=n<80000)
  if color<=7 and (face if kind=='Face' else hair): groups.setdefault((kind,n-color*(100 if face else 1)),set()).add(n)
batches=[];batch=[]
for key,ids in sorted(groups.items()):
 if len(ids)<2:continue
 if len(batch)+len(ids)>240:batches.append(batch);batch=[]
 batch+=sorted(ids)
if batch:batches.append(batch)
def run(task):
 i,ids=task;src=out/f'{i:03}.input';dst=out/f'{i:03}.result';src.write_text('\n'.join(map(str,ids)),encoding='ascii')
 proc=subprocess.run([str(exe),str(client),str(src),str(dst)],capture_output=True,timeout=180)
 if proc.returncode:raise RuntimeError((i,proc.returncode,proc.stdout[-200:],proc.stderr[-200:]))
 return i,len(ids)
with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
 for i,count in pool.map(run,enumerate(batches)):print(f'batch {i+1}/{len(batches)} checked {count}',flush=True)
good=set();failed=[]
for i in range(len(batches)):
 for line in (out/f'{i:03}.result').read_text(encoding='utf-8').splitlines():
  row=line.split()
  if row[1]=='OK':good.add(int(row[0]))
  else:failed.append(line)
catalog=[]
for ids in groups.values():
 passed=sorted(set(ids)&good)
 if len(passed)>=2:catalog+=passed
catalog=sorted(catalog)
(out/'verified-styles.txt').write_text('# Local BeiDou Data, audited with native PCOM and production 50:50 canvas composer.\n# 2026-10-03: each color was composed in BOTH directions against its family baseline.\n'+'\n'.join(map(str,catalog))+'\n',encoding='utf-8')
(out/'failures.txt').write_text('\n'.join(failed),encoding='utf-8')
print(json.dumps(dict(checked=sum(map(len,batches)),supported=len(catalog),failed=len(failed))),flush=True)
