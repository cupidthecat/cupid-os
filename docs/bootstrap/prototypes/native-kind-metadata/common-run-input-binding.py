from pathlib import Path
import base64
import hashlib
import json
import os
import queue
import subprocess
import tempfile
import threading
import time
import traceback

ROOT=Path(__file__).resolve().parents[2]
BASE=Path(__file__).resolve().parent
windows=os.name=='nt'
host,suffix=('windows','exe') if windows else ('linux','elf')
products = BASE / ('common-products-' + host + '1')
build_bytes=(products/'closed.json').read_bytes()
build=json.loads(build_bytes)
assert build['status']=='pass'
output = BASE / ('common-input-binding-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees=output/'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-input-binding-',dir='/var/tmp'))
trees.mkdir(exist_ok=True)
identity=lambda data:{'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
state={'schema':'cupid.input-binding-contracts.v1','status':'running','host':host,
       'build':identity(build_bytes),'runner':identity(Path(__file__).read_bytes()),
       'timeout_seconds':60,'linux_address_space_bytes':None if windows else 33554432,
       'linux_descriptor_limit':None if windows else 10240,'records':[]}
started=time.monotonic()

def guard():
    assert (products/'closed.json').read_bytes()==build_bytes
    assert identity((BASE/'common-source'/'input_binding_contract.cc').read_bytes())==build['sources']['input_binding_contract.cc']
    for name,facts in build['sources'].items(): assert identity((BASE/'common-source'/name).read_bytes())==facts
    for name,facts in build['normal_source_inputs'].items(): assert identity((ROOT/name).read_bytes())==facts
    for role in ['native','checked']:
        assert identity((products/(role+'-input-binding.'+suffix)).read_bytes())==build[role+'_images']['input-binding']

def bounds():
    import resource
    resource.setrlimit(resource.RLIMIT_AS,(33554432,33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE,(10240,10240))

def run(role,namespace,label,mode='same',same=1,expected=0,a='payload.bin',b='payload.bin',other=False,hard=False,alias=False,hold=False):
    guard()
    case='-'.join([role,namespace,label])
    container=trees/case
    working=container/'working \u03b4-\u732b'
    sibling=container/'other'
    working.mkdir(parents=True);sibling.mkdir()
    (working/'payload.bin').write_bytes(b'original-payload')
    (working/'different.bin').write_bytes(b'different-payload')
    (working/'directory').mkdir()
    if hard:
        os.link(working/'payload.bin', (sibling if other else working)/'hard.bin')
    if other and not hard: (sibling/'payload.bin').write_bytes(b'other-payload')
    root_a,root_b=working,sibling if other else working
    if alias:
        link=container/'alias'
        os.symlink(working,link,target_is_directory=True)
        root_b=link
    if mode=='collisions':
        for i in range(1025): (working/('entry-%04u.bin'%i)).write_bytes(str(i).encode())
    if namespace=='unc':
        root_a=Path('//localhost/C$'+root_a.as_posix()[2:])
        root_b=Path('//localhost/C$'+root_b.as_posix()[2:])
    argv=[str(products/(role+'-input-binding.'+suffix)),mode,
          str(root_a).encode('utf-8').hex(),str(root_b).encode('utf-8').hex(),a.encode().hex(),b.encode().hex(),str(same)]
    row={'case':case,'argv':argv,'expected_exit':expected,'expected_same':same,
         'collision_comparisons':1025 if mode=='collisions' else 0,'stale_validation_rejections':4 if hold else 0}
    state['records'].append(row)
    before=time.monotonic()
    process=subprocess.Popen(argv,cwd=ROOT,env={**os.environ,**build['producer_sentinels']},
                             stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                             preexec_fn=None if windows else bounds)
    prefix=b''
    try:
        if hold:
            packets=queue.Queue()
            threading.Thread(target=lambda:packets.put(process.stdout.readline()),daemon=True).start()
            prefix=packets.get(timeout=max(.001,60-(time.monotonic()-before)))
            assert prefix.rstrip(b'\r\n')==b'ready'
            (working/'payload.bin').write_bytes(b'changed-payload-with-new-size')
            process.stdin.write(b'x\n');process.stdin.flush()
        stdout,stderr=process.communicate(timeout=max(.001,60-(time.monotonic()-before)))
        raw=prefix+stdout
        row.update(exit_code=process.returncode,seconds=time.monotonic()-before,
                   stdout=identity(raw),stderr=identity(stderr),stdout_base64=base64.b64encode(raw).decode())
        (output/(case+'-stdout.bin')).write_bytes(raw);(output/(case+'-stderr.bin')).write_bytes(stderr)
        assert process.returncode==expected and not stderr and stdout.replace(b'\r\n',b'\n')==(b'accept\n' if expected==0 else b'reject\n') and row['seconds']<60,(case,process.returncode,stdout,stderr)
        guard()
    except BaseException as error:
        process.kill();stdout,stderr=process.communicate()
        row.update(error=repr(error),seconds=time.monotonic()-before,exit_code=process.returncode)
        (output/(case+'-failure-stdout.bin')).write_bytes(prefix+stdout)
        (output/(case+'-failure-stderr.bin')).write_bytes(stderr)
        raise
    finally:
        (output/'running.json').write_text(json.dumps(state,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')

try:
    for role in ['native','checked']:
        for namespace in ['drive','unc'] if windows else ['posix']:
            run(role,namespace,'same-file')
            run(role,namespace,'same-parent-hardlink',hard=True,b='hard.bin')
            run(role,namespace,'different-file',same=0,b='different.bin')
            run(role,namespace,'different-parent-hardlink',same=0,other=True,hard=True,b='hard.bin')
            run(role,namespace,'different-parent-file',same=0,other=True)
            run(role,namespace,'parent-alias',alias=True)
            run(role,namespace,'collisions',mode='collisions')
            run(role,namespace,'cached-after-metadata',mode='hold',hold=True)
            run(role,namespace,'duplicate-retains-old-capture',mode='duplicate-hold',hold=True)
            for invalid in ['null-left','null-right','null-output','null-logical','malformed','unobserved-left','unobserved-right']:
                run(role,namespace,invalid,mode=invalid,expected=1)
            run(role,namespace,'directory',a='directory',expected=1)
            run(role,namespace,'missing',a='missing.bin',expected=1)
    state.update(status='pass',calls=len(state['records']),
                 positive_calls=sum(r['expected_exit']==0 for r in state['records']),
                 negative_calls=sum(r['expected_exit']==1 for r in state['records']),
                 collision_comparisons=sum(r['collision_comparisons'] for r in state['records']),
                 stale_validation_rejections=sum(r['stale_validation_rejections'] for r in state['records']))
except BaseException as error:
    state.update(status='fail',error=repr(error));traceback.print_exc()
state['seconds']=time.monotonic()-started
(output/'closed.json').write_text(json.dumps(state,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({k:state.get(k) for k in ['status','host','calls','positive_calls','negative_calls','collision_comparisons','stale_validation_rejections','seconds','error']}))
raise SystemExit(0 if state['status']=='pass' else 1)
