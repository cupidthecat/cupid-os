from pathlib import Path
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES

application_bytes = read(ROOT / 'build/native-input-sharing-20261010/preceding-checkpoint-controls.json')
application = json.loads(application_bytes)
assert application['status'] == 'accepted-checkpoint'

def guard_root_application():
    assert read(ROOT / 'build/native-input-sharing-20261010/preceding-checkpoint-controls.json') == application_bytes
    for name, facts in application['projected_files'].items(): assert identity(read(ROOT / name)) == facts

windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
products = BASE / ('common-products-' + host + '1')
build = json.loads(read(products / 'closed.json'))
assert build['status'] == 'pass'
OUTPUT = BASE / ('common-root-contracts-' + host + '1')
assert not OUTPUT.exists()
OUTPUT.mkdir()
trees = OUTPUT / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-source-root-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
qualification = json.loads(read(BASE / 'source-controls.json'))
sentinels = {name:'forbidden-host-' + name for name in ('CC','CXX','CPP','AS','AR','LD','NM','OBJCOPY','NASM')}
environment = {**os.environ, **sentinels}
records, baselines, oracles = [], [], []
receipt = {'schema':'cupid.native-source-root-contracts.v1', 'status':'running', 'host':host,
    'runner':identity(read(Path(__file__))), 'build':identity(read(products / 'closed.json')),
    'tree_root':str(trees), 'records':records, 'baselines':baselines, 'actual_python_oracles':oracles,
    'producer_sentinels':sentinels, 'timeout_seconds':60, 'linux_address_space_bytes':33554432,
    'linux_descriptor_limit':10240}
started = time.monotonic()


def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432,33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240,10240))


def guard():
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    for role, facts in {**{'native-'+k:v for k,v in build['native_images'].items()},
                         **{'checked-'+k:v for k,v in build['checked_images'].items()}}.items():
        assert identity(read(products / (role+'.'+suffix))) == facts
    for name, facts in qualification['source_inputs'].items(): assert identity(read(ROOT / name)) == facts
    guard_root_application()


def alias_make(alias, target):
    assert alias.parent.resolve().is_relative_to(trees.resolve())
    if windows:
        result = subprocess.run(['cmd.exe','/d','/c','mklink','/J',str(alias),str(target)],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
        assert result.returncode == 0, (result.returncode,result.stdout,result.stderr)
    else: alias.symlink_to(target,target_is_directory=True)


def alias_remove(alias, target):
    assert alias.parent.resolve().is_relative_to(trees.resolve()) and alias.resolve() == target.resolve()
    if windows:
        assert getattr(alias.lstat(),'st_file_attributes',0)&1024
        alias.rmdir()
    else:
        assert alias.is_symlink()
        alias.unlink()


def case_tree(label):
    box = trees / ('case-%04d-%s' % (len(records),label))
    box.mkdir()
    for name in ('source-a','source-b'):
        root = box / name / 'nested'
        root.mkdir(parents=True)
        (root / 'payload.bin').write_bytes(b'payload-original-0000')
        (root / 'owned-output.img').write_bytes(b'old-output')
    return box


def call(role,label,mode,requested,physical,logical='payload.bin',expected=0,mutation=None):
    guard()
    request_bytes = requested if isinstance(requested,bytes) else str(requested).encode('utf-8')
    expected_bytes = str(physical).replace('\\','/') .encode('utf-8') if windows else str(physical).encode('utf-8')
    argv = [str(products / (role+'-source-root.'+suffix)),mode,request_bytes.hex(),logical.encode('utf-8').hex(),expected_bytes.hex()]
    before = time.monotonic()
    row = {'role':role,'label':label,'mode':mode,'root_hex':request_bytes.hex(),'logical_hex':logical.encode('utf-8').hex(),
        'expected_physical_hex':expected_bytes.hex(),'expected_exit_code':expected,
        'timeout_seconds':60,'address_space_bytes':None if windows else 33554432,
        'descriptor_limit':None if windows else 10240}
    prefix = b''
    if mutation:
        process = subprocess.Popen(argv,cwd=ROOT,env=environment,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
            preexec_fn=None if windows else limits)
        ready = threading.Event()
        packet = []
        def take_ready():
            packet.append(process.stdout.read(5)); ready.set()
        threading.Thread(target=take_ready,daemon=True).start()
        if not ready.wait(max(0.001,60-(time.monotonic()-before))) or packet != [b'ready']:
            process.kill(); process.communicate(); raise AssertionError(('readiness',row,packet))
        prefix = b'ready'
        row['mutation'] = mutation()
        stdout,stderr = process.communicate(input=b'v',timeout=max(0.001,60-(time.monotonic()-before)))
        exit_code = process.returncode
    else:
        result = subprocess.run(argv,cwd=ROOT,env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60,
            preexec_fn=None if windows else limits)
        stdout,stderr,exit_code = result.stdout,result.stderr,result.returncode
    stdout = prefix + stdout
    expected_stdout = (b'ready' if mutation else b'') + (b'accept' if expected == 0 else b'reject')
    if expected == 2 and not mutation: expected_stdout = b''
    row.update(exit_code=exit_code,stdout=stdout.decode('ascii'),stderr=stderr.decode('utf-8',errors='replace'),
        expected_stdout=expected_stdout.decode('ascii'),seconds=time.monotonic()-before,
        stdout_identity=identity(stdout),stderr_identity=identity(stderr),status='pass' if exit_code==expected and stdout==expected_stdout and not stderr else 'fail')
    records.append(row)
    (OUTPUT / 'running.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
    assert row['status']=='pass' and row['seconds'] < 60,row
    guard()


try:
    for role in ('native','checked'):
        for shape in ('ordinary','parent-alias','final-alias','unicode','space','dots','double-root', *(() if windows else ('colon','backslash','newline'))):
            box = case_tree(shape)
            target = box / 'source-a/nested'
            requested = target
            if shape=='parent-alias':
                alias_make(box/'alias',box/'source-a'); requested=box/'alias/nested'
            elif shape=='final-alias':
                alias_make(box/'alias',target); requested=box/'alias'
            elif shape in ('unicode','space','colon','backslash','newline'):
                name = {'unicode':'unicode-Î´-çŒ«','space':'space root','colon':'colon:root','backslash':'back\\root','newline':'line\nroot'}[shape]
                requested = box/name
                requested.mkdir(); (requested/'payload.bin').write_bytes(b'payload-original-0000'); (requested/'owned-output.img').write_bytes(b'old-output')
            elif shape=='dots': requested=str(target)+'/../nested/./'
            elif shape=='double-root' and not windows: requested='/'+str(target)
            physical=Path(requested).resolve(strict=True)
            assert physical.is_dir()
            oracles.append({'role':role,'label':shape,'requested':str(requested),'resolved':str(physical),'payload':identity(read(physical/'payload.bin'))})
            call(role,shape,'validate',requested,physical)
            call(role,shape+'-identity','same-root',requested,physical)
            if shape in ('parent-alias','final-alias'):
                old=products/(role+'-source-names.'+suffix)
                before=time.monotonic()
                old_result=subprocess.run([str(old),'source','open',str(requested).encode('utf-8').hex(),''],cwd=ROOT,env=environment,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60,preexec_fn=None if windows else limits)
                assert old_result.returncode==2 and old_result.stdout==old_result.stderr==b''
                baselines.append({'role':role,'shape':shape,'exit_code':old_result.returncode,'stdout':'','stderr':'','seconds':time.monotonic()-before,'timeout_seconds':60})
        for mode in ('null-root','null-owner','null-physical','zero-quota','max-quota'):
            box=case_tree(mode); call(role,mode,mode,box/'source-a/nested','',expected=2)
        for label,request in (('relative','relative/root'),('empty',''),('overflow',b'/'+b'x'*8191),
            ('bad-continuation',b'/bad-\xc2'),('surrogate',b'/bad-\xed\xa0\x80'),('above-scalar',b'/bad-\xf4\x90\x80\x80'),('overlong',b'/bad-\xc0\xaf')):
            call(role,label,'validate',request,'',expected=2)
        for label in ('missing','file-root','broken-alias'):
            box=case_tree(label); requested=box/'missing'
            if label=='file-root': requested=box/'source-a/nested/payload.bin'
            elif label=='broken-alias': alias_make(box/'alias',box/'missing'); requested=box/'alias'
            call(role,label,'validate',requested,'',expected=2)
        for mode in ('validate','borrow'):
            for mutation_kind in ('unchanged','retarget','remove','broken','directory-replacement','payload-restored-time'):
                box=case_tree(mode+'-'+mutation_kind)
                alias=box/'alias'; target=box/'source-a'; alias_make(alias,target)
                requested=alias/'nested'; physical=Path(requested).resolve(strict=True)
                def mutate():
                    if mutation_kind=='unchanged': return {'kind':mutation_kind}
                    if mutation_kind=='payload-restored-time':
                        path=physical/'payload.bin'; info=path.stat(); path.write_bytes(b'Payload-original-0000'); os.utime(path,ns=(info.st_atime_ns,info.st_mtime_ns))
                        return {'kind':mutation_kind,'size_preserved':path.stat().st_size==info.st_size,'mtime_preserved':path.stat().st_mtime_ns==info.st_mtime_ns}
                    alias_remove(alias,target)
                    if mutation_kind=='retarget': alias_make(alias,box/'source-b')
                    elif mutation_kind=='broken': alias_make(alias,box/'missing')
                    elif mutation_kind=='directory-replacement':
                        (alias/'nested').mkdir(parents=True); (alias/'nested/payload.bin').write_bytes(b'payload-original-0000')
                    return {'kind':mutation_kind,'original_physical_payload':identity(read(physical/'payload.bin'))}
                call(role,mode+'-'+mutation_kind,'hold-'+mode,requested,physical,expected=0 if mutation_kind=='unchanged' else 2,mutation=mutate)
    guard()
    receipt.update(status='pass',calls=len(records),positive_calls=sum(r['expected_exit_code']==0 for r in records),
        negative_calls=sum(r['expected_exit_code']!=0 for r in records),baseline_alias_rejections=len(baselines),
        normal_source_inputs_unchanged=99,installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail',error=repr(error)); traceback.print_exc()
receipt['seconds']=time.monotonic()-started
(OUTPUT/'closed.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({k:receipt.get(k) for k in ('status','calls','positive_calls','negative_calls','baseline_alias_rejections','error','seconds')}))
raise SystemExit(0 if receipt['status']=='pass' else 1)
