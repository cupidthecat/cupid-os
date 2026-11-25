from pathlib import Path
import ast,hashlib,json,os,subprocess,sys,tempfile,threading,time,traceback
ROOT=Path(__file__).resolve().parents[2];BASE=Path(__file__).resolve().parent
windows=os.name=='nt';host,suffix=('windows','exe') if windows else ('linux','elf')
products=BASE/('stage-capacity-products-'+host+'1');build_bytes=(products/'closed.json').read_bytes();build=json.loads(build_bytes)
assert build['status']=='pass'
output=BASE/('stage-capacity-contracts-'+host+'1');assert not output.exists();output.mkdir()
trees=output/'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-stage-capacity-',dir='/var/tmp'));trees.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT))
from tools import hostbuild
from replay_guard import make_guard
require_captured_inputs = make_guard(ROOT, BASE)
identity=lambda b:{'size':len(b),'sha256':hashlib.sha256(b).hexdigest()}
hostbuild_bytes=(ROOT/'tools/hostbuild.py').read_bytes();tree=ast.parse(hostbuild_bytes.decode('utf-8'))
function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='create_or_update_image')
selection_loop=next(n for n in ast.walk(function) if isinstance(n,ast.For) and isinstance(n.iter,ast.Name) and n.iter.id=='stage_files')
selection=compile(ast.Module([selection_loop],[]),'tools/hostbuild.py:stage-selection','exec')
missing_loop=next(n for n in ast.walk(function) if isinstance(n,ast.For) and isinstance(n.iter,ast.Name) and n.iter.id=='missing_stages' and any(isinstance(c,ast.If) for c in n.body))
condition=next(n.test for n in missing_loop.body if isinstance(n,ast.If));assert ast.unparse(condition)=="stage.source.exists() or _disk_is_link_or_junction(stage.source, 'missing stage input')"
predicate=compile(ast.Expression(condition),'tools/hostbuild.py:missing-stage-publication','eval')
sentinels={n:'forbidden-host-'+n for n in ('CC','CXX','CPP','AS','AR','LD','NM','OBJCOPY','NASM')}
records=[];started=time.monotonic()
receipt={'schema':'cupid.native-source-stage-capacity-contracts.v1','status':'running','host':host,'build':identity(build_bytes),'hostbuild':identity(hostbuild_bytes),
         'records':records,'runner':identity(Path(__file__).read_bytes()),'producer_sentinels':sentinels,'timeout_seconds':60,'linux_address_space_bytes':33554432,'linux_descriptor_limit':10240}
def guard():
 require_captured_inputs()
 assert (products/'closed.json').read_bytes()==build_bytes and (ROOT/'tools/hostbuild.py').read_bytes()==hostbuild_bytes
 for role in ('native','checked'):assert identity((products/(role+'-stage-capacity.'+suffix)).read_bytes())==build[role+'_image']
 assert identity((products/'stage-capacity.o').read_bytes())==build['object']
def limits():
 import resource
 resource.setrlimit(resource.RLIMIT_AS,(33554432,33554432));resource.setrlimit(resource.RLIMIT_NOFILE,(10240,10240))
def ready(process,before):
 event,packets=threading.Event(),[]
 def read():packets.append(process.stdout.read(5));event.set()
 threading.Thread(target=read,daemon=True).start()
 assert event.wait(max(.001,60-(time.monotonic()-before))) and packets==[b'ready'],packets
try:
 guard()
 for role in ('native','checked'):
  for namespace in ('drive','unc') if windows else ('posix',):
   for shape in ('flat','nested'):
    for mutation in ('unchanged','parents','last-leaf'):
     root=trees/(role+'-'+namespace+'-'+shape+'-'+mutation);root.mkdir()
     if namespace=='unc':root=Path('//localhost/C$'+root.as_posix()[2:])
     requests=[root/('ghost/d%d/deep/payload.bin'%i if shape=='nested' else 'ghost/p%d.bin'%i) for i in range(4096)]
     assert len({str(p) for p in requests})==4096
     env={'stage_files':[hostbuild.StageFile(p,'/p%d.bin'%i) for i,p in enumerate(requests)],'present_stages':[],'missing_stages':[],
          '_disk_absolute':hostbuild._disk_absolute,'_disk_is_link_or_junction':hostbuild._disk_is_link_or_junction,'_resolve_disk_regular':hostbuild._resolve_disk_regular,'DiskImageError':hostbuild.DiskImageError,'StageFile':hostbuild.StageFile}
     exec(selection,env);assert not env['present_stages'] and len(env['missing_stages'])==4096
     program=products/(role+'-stage-capacity.'+suffix);argv=[str(program),shape,str(root).encode('utf-8').hex()]
     before=time.monotonic();process=subprocess.Popen(argv,cwd=ROOT,env={**os.environ,**sentinels},stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=None if windows else limits)
     try:
      ready(process,before)
      if mutation=='parents':
       for path in requests:path.parent.mkdir(parents=True,exist_ok=True)
      elif mutation=='last-leaf':requests[-1].parent.mkdir(parents=True,exist_ok=True);requests[-1].write_bytes(b'appeared')
      decisions=[]
      for stage in env['missing_stages']:
       decisions.append(bool(eval(predicate,{'stage':stage,'_disk_is_link_or_junction':hostbuild._disk_is_link_or_junction})))
      assert sum(decisions)==(1 if mutation=='last-leaf' else 0)
      stdout,stderr=process.communicate(input=b'v',timeout=max(.001,60-(time.monotonic()-before)))
     except BaseException:process.kill();process.communicate();raise
     expected=2 if mutation=='last-leaf' else 0
     row={'role':role,'namespace':namespace,'shape':shape,'mutation':mutation,'argv':argv,'distinct_requests':4096,'original_python_missing':4096,
          'requests':[str(stage.source) for stage in env['missing_stages']],'python_final_rejects':decisions,'expected_exit':expected,'exit_code':process.returncode,'stdout':(b'ready'+stdout).decode('ascii'),'stderr':stderr.decode('utf-8'),
          'seconds':time.monotonic()-before,'timeout_seconds':60,'linux_address_space_bytes':None if windows else 33554432,'linux_descriptor_limit':None if windows else 10240}
     records.append(row)
     (output/'running.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
     assert process.returncode==expected and stdout==(b'reject' if expected else b'accept') and stderr==b'' and row['seconds']<60,row
     guard()
 receipt.update(status='pass',calls=len(records),positive_calls=sum(r['expected_exit']==0 for r in records),negative_calls=sum(r['expected_exit']==2 for r in records),distinct_requests_per_call=4096,python_selection_calls=4096*len(records),python_publication_calls=4096*len(records))
except BaseException as error:receipt.update(status='fail',error=repr(error));traceback.print_exc()
receipt['seconds']=time.monotonic()-started
(output/'closed.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({k:receipt.get(k) for k in ('status','host','calls','positive_calls','negative_calls','seconds','error')}));raise SystemExit(0 if receipt['status']=='pass' else 1)
