from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,time,traceback
ROOT=Path(__file__).resolve().parents[2];BASE=Path(__file__).resolve().parent
windows=os.name=='nt';host,suffix,index=('windows','exe','1') if windows else ('linux','elf','1')
products=BASE/('stage-products-'+host+index);build_bytes=(products/'closed.json').read_bytes();build=json.loads(build_bytes)
assert build['status']=='pass'
OUTPUT=BASE/('stage-capacity-products-'+host+'1');assert not OUTPUT.exists();OUTPUT.mkdir()
sys.path.insert(0,str(ROOT))
from tools import bootstrap_toolchain as bootstrap
from replay_guard import make_guard
require_captured_inputs = make_guard(ROOT, BASE)
read=lambda path:Path(path).read_bytes();identity=lambda b:{'size':len(b),'sha256':hashlib.sha256(b).hexdigest()}
fixture=BASE/'source-stage-capacity-contract.cc';fixture_bytes=read(fixture)
baseline=BASE/('root-products-'+host+'1')
baseline_bytes=read(baseline/'closed.json');baseline_record=json.loads(baseline_bytes)
assert identity(baseline_bytes)==build['root_build']
seed=bootstrap.freeze_seed_inputs(ROOT/('bootstrap/seeds/i386-'+host+'/manifest.json'),OUTPUT/'seed');tools=seed.tools
sentinels={n:'forbidden-host-'+n for n in ('CC','CXX','CPP','AS','AR','LD','NM','OBJCOPY','NASM')}
commands=[];started=time.monotonic()
receipt={'schema':'cupid.native-source-stage-capacity-build.v1','status':'running','host':host,'core_build':identity(build_bytes),
         'fixture':identity(fixture_bytes),'runtime_build':identity(baseline_bytes),'producer_sentinels':sentinels,'commands':commands,'runner':identity(read(Path(__file__)))}
def guard():
 require_captured_inputs()
 assert read(products/'closed.json')==build_bytes and read(baseline/'closed.json')==baseline_bytes and read(fixture)==fixture_bytes
 bootstrap.require_live_seed_inputs(seed)
 for n,f in build['sources'].items():assert identity(read(products/'source'/n))==f,n
 for n,f in build['objects'].items():assert identity(read(products/'objects'/n))==f,n
 for n,f in baseline_record['objects'].items():assert identity(read(baseline/'objects'/n))==f,n
 for n,f in build['normal_source_inputs'].items():assert identity(read(ROOT/n))==f,n
 for n,f in build['support_inputs'].items():assert identity(read(ROOT/n))==f,n

def run(label,argv,native=False):
 guard();before=time.monotonic()
 result=subprocess.run(list(map(str,argv)),cwd=ROOT,env=dict(os.environ) if native else {**os.environ,**sentinels},capture_output=True,timeout=180)
 (OUTPUT/(label+'-stdout.bin')).write_bytes(result.stdout);(OUTPUT/(label+'-stderr.bin')).write_bytes(result.stderr)
 commands.append({'label':label,'argv':list(map(str,argv)),'native':native,'exit_code':result.returncode,'seconds':time.monotonic()-before,'timeout_seconds':180,'stdout':identity(result.stdout),'stderr':identity(result.stderr)})
 assert result.returncode==0 and result.stdout==result.stderr==b'',commands[-1]
 guard()
try:
 guard();source=products/'source';compiler=shutil.which('clang');assert compiler
 common=[compiler,'-x','c','-std=c11','-O2','-Wall','-Wextra','-Werror','-D_CRT_SECURE_NO_WARNINGS=1','-I',source,'-I',ROOT/'toolchain']
 if windows:common.append('-DCUPID_NATIVE_UTF8_ENABLE=1')
 native_objects=[products/('native-'+n+'.o') for n in ('cupidbuild_host','path_encoding','cupidbuild_source_path','cupidbuild_source_file','cupidbuild_source_stage')]
 if windows:native_objects.append(products/'native-utf8.o')
 native=OUTPUT/('native-stage-capacity.'+suffix)
 run('native-link',[*common,fixture,'-x','none',*native_objects,*(['-lntdll'] if windows else []),'-o',native],True)
 checked_object=OUTPUT/'stage-capacity.o'
 run('compile',[tools['cupidc'],'--root',ROOT,'-c','/'+fixture.relative_to(ROOT).as_posix(),'-I','/'+source.relative_to(ROOT).as_posix(),'-I','/toolchain','--include-angle','/toolchain/hosted/i386-linux/include','-o','/'+checked_object.relative_to(ROOT).as_posix()])
 bootstrap._validate_i386_relocatable(checked_object)
 plan=json.loads(read(ROOT/'bootstrap/seeds/i386-linux/manifest.json'))['build_plan']
 if windows:plan=bootstrap._windows_build_plan(plan,utf8=True,user_link_aliases=True)
 shared=[row['name'] for row in plan['sources'] if row['name'] in {'runtime','publication_runtime','cupidbuild_host','path_encoding','windows_utf8_build'}]
 assembly=plan.get('assembly_sources',[{'name':'start','path':'/toolchain/hosted/i386-linux/start.asm'}])
 order=['start','source-path','source-file','source-stage',*(['path-encoding'] if not windows else []),*shared,*(row['name'] for row in assembly if row['name']!='start'),'stage-capacity']
 obj={n:(products/'objects' if n+'.o' in build['objects'] else baseline/'objects')/(n+'.o') for n in order if n!='stage-capacity'}
 obj['stage-capacity']=checked_object;checked=OUTPUT/('checked-stage-capacity.'+suffix)
 args=bootstrap._windows_link_arguments('cupidbuild',checked,obj,order,utf8=True,user_link_aliases=True) if windows else ['-m','elf_i386','--text-address','0x08048000','--entry','_start','-o',checked,*(obj[n] for n in order)]
 run('link',[tools['cupidld'],*args])
 if windows:
  imports=tuple((lib,tuple(r['procedure'] for r in plan['imports']['cupidbuild'] if r['library']==lib)) for lib in sorted({r['library'] for r in plan['imports']['cupidbuild']}))
  bootstrap._validate_static_i386_pe32(checked,0x00401000,imports)
 else:bootstrap._validate_static_i386_elf(checked,0x08048000)
 for path in (checked_object,checked):run('strict-'+path.name,[tools['cupiddis'],'--require-known','--require-local-targets','--require-code-anchors',path])
 receipt.update(status='pass',object=identity(read(checked_object)),native_image=identity(read(native)),checked_image=identity(read(checked)),native_object_controls={p.name:identity(read(p)) for p in native_objects})
except BaseException as error:receipt.update(status='fail',error=repr(error));traceback.print_exc()
receipt['seconds']=time.monotonic()-started
(OUTPUT/'closed.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({k:receipt.get(k) for k in ('status','host','seconds','error')}));raise SystemExit(0 if receipt['status']=='pass' else 1)
