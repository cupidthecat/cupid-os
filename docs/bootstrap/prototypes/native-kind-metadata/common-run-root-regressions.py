from pathlib import Path
import concurrent.futures,hashlib,json,os,subprocess,sys,threading,time,traceback
ROOT=Path(__file__).resolve().parents[2];BASE=Path(__file__).resolve().parent
host='windows' if os.name=='nt' else 'linux'
output=BASE/('common-root-regressions-' + host + '1');assert not output.exists();output.mkdir()
read=lambda p:Path(p).read_bytes();identity=lambda b:{'size':len(b),'sha256':hashlib.sha256(b).hexdigest()}
products=BASE/('common-products-' + host + '1');build_bytes=read(products/'closed.json');build=json.loads(build_bytes)
assert build['status']=='pass'
application_bytes=read(BASE/'preceding-checkpoint-controls.json');application=json.loads(application_bytes)
selections=[('source-root','common-root-run-source-root-contracts.py',[],'root-contracts'),
 ('source-profile','common-root-run-source-observer-contracts.py',[],'source-contracts'),
 ('missing-unc','common-root-run-missing-unc-contracts-second.py' if host=='windows' else 'common-root-run-missing-unc-contracts.py',[],'missing-unc'),
 ('resolved-parent','common-root-run-resolved-parent-contracts.py',[],'resolved-parent'),
 ('contracts','common-root-run-retained-contracts.py',[],'contracts'),('legacy','common-root-run-legacy-contracts.py',['1'],'legacy'),
 ('alias','common-root-run-alias-regressions.py',[],'alias')]
scripts={s:identity(read(BASE/s)) for _,s,_,_ in selections};scripts[Path(__file__).name]=identity(read(Path(__file__)))
normal=json.loads(read(BASE/'source-controls.json'))['source_inputs']
sentinels={n:'forbidden-host-'+n for n in ('CC','CXX','CPP','AS','AR','LD','NM','OBJCOPY','NASM')}
commands=[];lock=threading.Lock();started=time.monotonic()
receipt={'schema':'cupid.native-source-stage-root-regressions.v1','status':'running','host':host,'build':identity(build_bytes),
         'application':identity(application_bytes),'scripts':scripts,'commands':commands,'producer_sentinels':sentinels,'workers':2}
def guard():
 assert read(products/'closed.json')==build_bytes and read(BASE/'preceding-checkpoint-controls.json')==application_bytes
 for n,f in application['projected_files'].items():assert identity(read(ROOT/n))==f,n
 for n,f in normal.items():assert identity(read(ROOT/n))==f,n
 for n,f in scripts.items():assert identity(read(BASE/n))==f,n
 for n,f in build['sources'].items():assert identity(read(products/'source'/n))==f,n
 for n,f in build['objects'].items():assert identity(read(products/'objects'/n))==f,n
 for role in ('native','checked'):
  for n,f in build[role+'_images'].items():assert identity(read(products/(role+'-'+n+('.exe' if host=='windows' else '.elf'))))==f,n

def run(row):
 label,script,args,_=row;guard();before=time.monotonic();argv=[sys.executable,str(BASE/script),*args]
 result=subprocess.run(argv,cwd=ROOT,env={**os.environ,**sentinels},capture_output=True)
 for stream in ('stdout','stderr'):(output/(label+'-'+stream+'.bin')).write_bytes(getattr(result,stream))
 record={'label':label,'argv':argv,'exit_code':result.returncode,'seconds':time.monotonic()-before,'stdout':identity(result.stdout),'stderr':identity(result.stderr)}
 with lock:
  commands.append(record);(output/'running.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
 assert result.returncode==0,record
 guard()
try:
 guard()
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
  futures=[executor.submit(run,row) for row in selections]
  for future in futures:future.result()
 groups={label:json.loads(read(BASE/('common-'+directory+'-'+host+'1')/'closed.json')) for label,_,_,directory in selections}
 assert all(r['status']=='pass' for r in groups.values())
 expected={'source-root':82 if host=='windows' else 94,'source-profile':226 if host=='windows' else 338,'missing-unc':104 if host=='windows' else 64,'resolved-parent':22 if host=='windows' else 20,'contracts':92 if host=='windows' else 86}
 for label,count in expected.items():assert groups[label]['calls']==count,(label,groups[label]['calls'])
 assert groups['legacy']['selected_methods']==176 and groups['legacy']['passed_methods']==(167 if host=='windows' else 173) and groups['legacy']['declared_skips']==(9 if host=='windows' else 3)
 assert groups['alias']['selected_methods']==groups['alias']['passed_methods']==24 and groups['alias']['declared_skips']==0
 receipt.update(status='pass',child_receipts={label:identity(read(BASE/('common-'+directory+'-'+host+'1')/'closed.json')) for label,_,_,directory in selections},api_calls=sum(expected.values()),legacy_selected_methods=176,legacy_passed_methods=groups['legacy']['passed_methods'],legacy_declared_skips=groups['legacy']['declared_skips'],alias_passed_methods=24)
except BaseException as error:receipt.update(status='fail',error=repr(error));traceback.print_exc()
receipt['seconds']=time.monotonic()-started
(output/'closed.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({k:receipt.get(k) for k in ('status','host','seconds','api_calls','legacy_passed_methods','legacy_declared_skips','error')}));raise SystemExit(0 if receipt['status']=='pass' else 1)
