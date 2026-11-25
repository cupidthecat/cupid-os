from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import traceback

ROOT=Path(__file__).resolve().parents[2]
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
from tools import bootstrap_toolchain as bootstrap

identity=lambda data:{'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
read=lambda path:Path(path).read_bytes()
windows=os.name=='nt'
host,suffix=('windows','exe') if windows else ('linux','elf')
output=BASE/('common-products-'+host+'1')
assert not output.exists()
output.mkdir()
control_bytes=read(BASE/'common-controls.json')
accepted=json.loads(control_bytes)
assert accepted['status']=='prepared-not-tested'
source=output/'source'
shutil.copytree(BASE/'common-source',source)
assert {p.name:identity(read(p)) for p in source.iterdir()}==accepted['sources']
checkpoint_bytes=read(BASE/'preceding-checkpoint-controls.json')
checkpoint=json.loads(checkpoint_bytes)
assert checkpoint['status']=='accepted-checkpoint'
seed=bootstrap.freeze_seed_inputs(ROOT/('bootstrap/seeds/i386-'+host+'/manifest.json'),output/'seed')
sentinels=accepted['producer_sentinels']
objects=output/'objects'
objects.mkdir()
reused={}
contracts={'retained':'retained_observer_contract.cc','legacy-observer':'legacy_observer_contract.cc',
           'legacy-stream':'legacy_stream_contract.cc','missing-unc':'observer_missing_unc_contract.cc',
           'resolved-parent':'unc_resolved_parent_contract.cc','source-names':'source_observer_contract.cc',
           'source-root':'source_root_contract.cc','source-file':'source_file_contract.cc',
           'source-optional':'source_optional_contract.cc','source-stage':'source_stage_contract.cc',
           'input-selection':'source_selection_contract_eighth.cc',
           'input-binding':'input_binding_contract.cc',
           'input-request-binding':'input_request_binding_contract.cc'}
file_programs={'source-file','source-optional','source-stage','input-selection','input-binding','input-request-binding'}
state={'schema':'cupid.input-sharing-preceding-build.v1','status':'running','host':host,
       'source_controls':identity(control_bytes),'checkpoint_controls':identity(checkpoint_bytes),
       'sources':accepted['sources'],'normal_source_inputs':accepted['normal_source_inputs'],
       'producer_sentinels':sentinels,'reused_objects':reused,'commands':[],
       'runner':identity(read(Path(__file__)))}
started=time.monotonic()

def guard():
    bootstrap.require_live_seed_inputs(seed)
    assert read(BASE/'common-controls.json')==control_bytes and read(BASE/'preceding-checkpoint-controls.json')==checkpoint_bytes
    for name,facts in accepted['sources'].items():
        assert identity(read(BASE/'common-source'/name))==identity(read(source/name))==facts
    for name,facts in accepted['normal_source_inputs'].items():assert identity(read(ROOT/name))==facts
    for name,facts in checkpoint['projected_files'].items():assert identity(read(ROOT/name))==facts
    for name,facts in reused.items():assert identity(read(objects/name))==facts

def run(label,argv,native=False,timeout=180):
    guard()
    before=time.monotonic()
    result=subprocess.run(list(map(str,argv)),cwd=ROOT,env=dict(os.environ) if native else {**os.environ,**sentinels},
                          capture_output=True,timeout=timeout)
    for stream in ['stdout','stderr']:(output/(label+'-'+stream+'.bin')).write_bytes(getattr(result,stream))
    state['commands'].append({'label':label,'argv':list(map(str,argv)),'native':native,
        'exit_code':result.returncode,'timeout_seconds':timeout,'seconds':time.monotonic()-before,
        'stdout':identity(result.stdout),'stderr':identity(result.stderr)})
    (output/'running.json').write_text(json.dumps(state,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
    assert result.returncode==0 and result.stdout==result.stderr==b'',state['commands'][-1]
    guard()

try:
    guard()
    compiler=shutil.which('clang');assert compiler
    common=[compiler,'-x','c','-std=c11','-O2','-Wall','-Wextra','-Werror','-D_CRT_SECURE_NO_WARNINGS=1',
            '-I',source,'-I',ROOT/'toolchain']
    native_profiles={}
    modules=['cupidbuild_host','path_encoding','cupidbuild_source_path','cupidbuild_source_file','cupidbuild_source_stage']
    for profile in ['ordinary','utf8'] if windows else ['ordinary']:
        flags=['-DCUPID_NATIVE_UTF8_ENABLE=1'] if profile=='utf8' else []
        native=[]
        for name in modules:
            path=output/(('native-' if profile=='utf8' or not windows else 'ordinary-native-')+name+'.o')
            run('native-'+profile+'-'+name,[*common,*flags,'-c',source/(name+'.cc'),'-o',path],True)
            native.append(path)
        if profile=='utf8':
            path=output/'native-utf8.o'
            run('native-utf8-adapter',[*common,*flags,'-c',ROOT/'toolchain/native_utf8.cc','-o',path],True)
            native.append(path)
        native_profiles[profile]=native
    if windows:
        shutil.copyfile(native_profiles['ordinary'][0],output/'native-host.o')
        shutil.copyfile(native_profiles['ordinary'][1],output/'native-path.o')
    else:
        shutil.copyfile(native_profiles['ordinary'][0],output/'native-host.o')
        shutil.copyfile(native_profiles['ordinary'][1],output/'native-path.o')
    native_images={}
    for name,fixture in contracts.items():
        profile='utf8' if windows and name in file_programs else 'ordinary'
        flags=['-DCUPID_NATIVE_UTF8_ENABLE=1'] if profile=='utf8' else []
        if windows and name not in file_programs:flags.append('-DNATIVE_OBSERVER_WINDOWS_WRITER=1')
        image=output/('native-'+name+'.'+suffix)
        run('native-link-'+name,[*common,*flags,source/fixture,'-x','none',*native_profiles[profile],
                                 *(['-lntdll'] if windows else []),'-o',image],True)
        native_images[name]=identity(read(image))
    plan=json.loads(read(ROOT/'bootstrap/seeds/i386-linux/manifest.json'))['build_plan']
    if windows:plan=bootstrap._windows_build_plan(plan,utf8=True,user_link_aliases=True)
    shared=[row['name'] for row in plan['sources'] if row['name'] in
            {'runtime','publication_runtime','cupidbuild_host','path_encoding','windows_utf8_build'}]
    assembly=plan.get('assembly_sources',[{'name':'start','path':'/toolchain/hosted/i386-linux/start.asm'}])
    rows=[]
    for row in plan['sources']:
        if row['name'] in shared:
            path=source/Path(row['path']).name if row['name'] in {'cupidbuild_host','path_encoding'} else ROOT/row['path'].lstrip('/')
            rows.append((row['name'],path,row.get('definitions',[]),row.get('gnu_extensions',False)))
    rows.extend((name,source/file,[],False) for name,file in [
        ('source-path','cupidbuild_source_path.cc'),('source-file','cupidbuild_source_file.cc'),
        ('source-stage','cupidbuild_source_stage.cc'),*([('path-encoding','path_encoding.cc')] if not windows else [])])
    for name,path,definitions,gnu in rows:
        destination=objects/(name+'.o')
        argv=[seed.tools['cupidc'],'--root',ROOT,*(['--gnu'] if gnu else [])]
        for definition in definitions:argv.extend(['-D',definition])
        argv.extend(['-c','/'+path.relative_to(ROOT).as_posix(),'-I','/'+source.relative_to(ROOT).as_posix(),
            '-I','/toolchain','--include-angle','/toolchain/hosted/i386-linux/include',
            '-o','/'+destination.relative_to(ROOT).as_posix()])
        run('compile-common-'+name,argv,timeout=360)
        bootstrap._validate_i386_relocatable(destination)
    for row in assembly:
        run('assemble-common-'+row['name'],[seed.tools['cupidasm'],'-f','elf32',ROOT/row['path'].lstrip('/'),
            '-o',objects/(row['name']+'.o')],timeout=120)
    checked_images={}
    for name,fixture in contracts.items():
        object_name=name+'-contract' if name in file_programs else name
        path=objects/(object_name+'.o')
        assert not path.exists()
        run('compile-'+name,[seed.tools['cupidc'],'--root',ROOT,'-c','/'+(source/fixture).relative_to(ROOT).as_posix(),
            '-I','/'+source.relative_to(ROOT).as_posix(),'-I','/toolchain','--include-angle',
            '/toolchain/hosted/i386-linux/include','-o','/'+path.relative_to(ROOT).as_posix()])
        bootstrap._validate_i386_relocatable(path)
        owner=['source-path','source-file','source-stage',*(['path-encoding'] if not windows else [])] if name in file_programs else []
        order=['start',*owner,object_name,*shared,*(row['name'] for row in assembly if row['name']!='start')]
        obj={key:objects/(key+'.o') for key in order}
        assert all(p.exists() for p in obj.values())
        image=output/('checked-'+name+'.'+suffix)
        args=bootstrap._windows_link_arguments('cupidbuild',image,obj,order,utf8=True,user_link_aliases=True) if windows else [
            '-m','elf_i386','--text-address','0x08048000','--entry','_start','-o',image,*(obj[key] for key in order)]
        run('link-'+name,[seed.tools['cupidld'],*args])
        if windows:
            imports=tuple((lib,tuple(row['procedure'] for row in plan['imports']['cupidbuild'] if row['library']==lib))
                          for lib in sorted({row['library'] for row in plan['imports']['cupidbuild']}))
            bootstrap._validate_static_i386_pe32(image,0x00401000,imports)
        else:bootstrap._validate_static_i386_elf(image,0x08048000)
        checked_images[name]=identity(read(image))
    for path in [*sorted(objects.glob('*.o')),*(output/('checked-'+name+'.'+suffix) for name in contracts)]:
        run('strict-'+path.name,[seed.tools['cupiddis'],'--require-known','--require-local-targets','--require-code-anchors',path],timeout=120)
    guard()
    state.update(status='pass',native_images=native_images,checked_images=checked_images,
                 objects={p.name:identity(read(p)) for p in objects.iterdir()},
                 support_inputs={name:identity(read(ROOT/name)) for name in ['tools/bootstrap_toolchain.py','tools/hostbuild.py','toolchain/native_utf8.cc','toolchain/native_utf8.h']})
except BaseException as error:
    state.update(status='fail',error=repr(error));traceback.print_exc()
state['seconds']=time.monotonic()-started
(output/'closed.json').write_text(json.dumps(state,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({k:state.get(k) for k in ['status','host','seconds','error']}))
raise SystemExit(0 if state['status']=='pass' else 1)
