"""Rebuild held image-stage owners and replay their complete retained contracts."""
import argparse
import gzip
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback

created_output = None
CASE_KEYS = ('role','label','mode','profile','expected_exit_code','expected_exit','expected_stdout',
             'distinct_missing_paths','distinct_requests','original_python_missing','namespace','shape','mutation',
             'tree_files','invalid','names_hex','logical_hex','expected_kind','exit_code','stdout','stderr','status','test','group')
def identity(data):
    return {'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
def case_plan(record):
    return [{key:row[key] for key in CASE_KEYS if key in row} for row in record['records']]

def main():
    global created_output
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();artifact=Path(__file__).resolve().parent;root=artifact.parents[2]
    output=args.output.resolve()
    if output.parent!=root/'build' or output.exists() or output.is_symlink():
        raise ValueError('Use a fresh direct child of the repository build directory')
    output.mkdir();created_output=output
    proof_path=artifact.parent/'evidence/native-source-stage-20261010.json'
    proof_bytes=proof_path.read_bytes();proof=json.loads(proof_bytes)
    if proof['status'] not in ('paired-acceptance-complete-repository-replay-pending','pass') or proof['complete_current_api_calls']!=2054 or proof['source_files']!=21:
        raise ValueError('The complete original stage-owner acceptance differs')
    if identity(Path(__file__).read_bytes())!=proof['replay_runner']:
        raise ValueError('The reviewed replay runner differs')
    captured={proof_path:proof_bytes,Path(__file__):Path(__file__).read_bytes()}
    archive_path=artifact.parent/'evidence/native-source-stage-originals-20261010.json.gz'
    archive_bytes=archive_path.read_bytes()
    if identity(archive_bytes)!=proof['original_receipts_archive'] or identity(gzip.decompress(archive_bytes))!=proof['original_receipts_uncompressed']:
        raise ValueError('The complete original receipt archive differs')
    captured[archive_path]=archive_bytes
    patch_path=artifact/'native-source-stage.patch';patch_bytes=patch_path.read_bytes()
    if identity(patch_bytes)!=proof['patch']:raise ValueError('The combined source patch differs')
    captured[patch_path]=patch_bytes
    helper_folder=artifact/'native-source-stage'
    expected_helpers={'consumer_audit_common.py','patch_sources.py','replay_guard.py','build-root-regressions.py','run-source-root-contracts.py',
                      'run-source-observer-contracts.py','run-missing-unc-contracts.py','run-resolved-parent-contracts.py','run-retained-contracts.py',
                      'run-legacy-contracts.py','run-alias-regressions.py','run-required-file-contracts.py','run-optional-contracts.py',
                      'build-source-stage.py','run-source-stage.py','build-stage-capacity.py','run-stage-capacity.py'}
    if set(proof['replay_helpers'])!=expected_helpers:raise ValueError('The complete helper inventory differs')
    for name,facts in proof['replay_helpers'].items():
        path=helper_folder/name;data=path.read_bytes()
        if identity(data)!=facts:raise ValueError('Replay helper differs: '+name)
        captured[path]=data;(output/name).write_bytes(data);captured[output/name]=data
    sys.path.insert(0,str(output));common=importlib.import_module('consumer_audit_common');patcher=importlib.import_module('patch_sources')
    if len(proof['root_files'])!=20 or len(proof['source_inputs'])!=99:raise ValueError('The complete root/input inventories differ')
    for name,facts in proof['root_files'].items():
        path=root/name;data=common.read(path)
        if identity(data)!=facts or data!=common.git_bytes('HEAD',name) or data!=common.git_bytes(proof['checkpoint'],name):
            raise ValueError('Committed root projection differs: '+name)
        captured[path]=data
    for name,facts in proof['source_inputs'].items():
        path=root/name;data=common.read(path)
        if identity(data)!=facts or data!=common.git_bytes(proof['source_revision'],name):raise ValueError('Normal producer input differs: '+name)
        captured[path]=data
    for name,facts in proof['support_inputs'].items():
        path=root/name;data=common.read(path)
        if identity(data)!=facts:raise ValueError('Replay support differs: '+name)
        captured[path]=data
    extraction=output/'patch-sources';extraction.mkdir()
    patched_expected={name:row['identity'] for name,row in proof['patched_sources'].items()}
    patcher.apply_sources(patch_bytes,root,extraction,patched_expected,proof['patched_sources'])
    sources=output/'source3';sources.mkdir();sys.path.insert(0,str(root))
    for name,row in proof['patched_sources'].items():
        data=common.read(extraction/name);captured[extraction/name]=data
        target=output/name if name=='source-stage-capacity-contract.cc' else sources/name
        target.write_bytes(data);captured[target]=data
    for name,row in proof['supplemental_sources'].items():
        if 'path' in row:
            path=root/row['path'];data=common.read(path);captured[path]=data
        else:
            module=importlib.import_module(row['module']);data=getattr(module,row['constant']).encode('ascii')
            if Path(module.__file__) not in captured:raise ValueError('Supplemental caller module is not guarded')
        if identity(data)!=row['identity']:raise ValueError('Supplemental source differs: '+name)
        (sources/name).write_bytes(data);captured[sources/name]=data
    options_proof_path=artifact.parent/'evidence/native-image-options-20261010.json'
    options_proof_bytes=common.read(options_proof_path);options=json.loads(options_proof_bytes)
    options_path=artifact/'native-image-options.patch';options_bytes=common.read(options_path)
    if options['status']!='pass' or identity(options_bytes)!=options['artifact_patch'] or options['sources']!=proof['options_support']:
        raise ValueError('The nine held options controls differ')
    captured[options_proof_path]=options_proof_bytes;captured[options_path]=options_bytes
    lines=options_bytes.decode('utf-8').splitlines(True);cursor=0;seen=set()
    while cursor<len(lines):
        if lines[cursor]!='--- /dev/null\n' or not lines[cursor+1].startswith('+++ b/'):
            raise ValueError('Unexpected options patch header')
        name=Path(lines[cursor+1][6:].strip()).name;match=re.fullmatch(r'@@ -0,0 \+1,(\d+) @@\n',lines[cursor+2])
        if not match or name not in proof['options_support'] or name in seen:raise ValueError('Unexpected options source')
        count=int(match[1]);body=lines[cursor+3:cursor+3+count]
        if len(body)!=count or any(not line.startswith('+') for line in body):raise ValueError('Incomplete options source')
        data=''.join(line[1:] for line in body).encode('utf-8')
        if identity(data)!=proof['options_support'][name]:raise ValueError('Complete options source differs: '+name)
        (sources/name).write_bytes(data);captured[sources/name]=data;seen.add(name);cursor+=3+count
    if seen!=set(proof['options_support']) or {p.name:identity(common.read(p)) for p in sources.iterdir()}!=proof['core_sources']:
        raise ValueError('The complete twenty-nine-source copy differs')
    source_controls={key:proof[key] for key in ('source_revision','source_snapshot_sha256','source_inputs')}
    controls={'schema':'cupid.native-source-stage-replay-controls.v1','status':'pass',**source_controls,'root_files':proof['root_files'],
              'support_inputs':proof['support_inputs'],'sources':proof['core_sources'],
              'accepted_root_builds':{host:proof['original_hosts'][host]['root_build'] for host in ('linux','windows')}}
    for name,record in (('source-controls.json',source_controls),('replay-controls.json',controls)):
        data=(json.dumps(record,indent=2,sort_keys=True)+'\n').encode('utf-8');(output/name).write_bytes(data);captured[output/name]=data
    from tools import bootstrap_toolchain as bootstrap
    windows=os.name=='nt';host,suffix=('windows','exe') if windows else ('linux','elf');expected=proof['original_hosts'][host]
    seed=bootstrap.freeze_seed_inputs(root/('bootstrap/seeds/i386-'+host+'/manifest.json'),output/'seed')
    if {name:identity(common.read(path)) for name,path in seed.tools.items()}!=expected['root_build']['actual_installed_tools']:
        raise ValueError('The complete execution cohort differs')
    sentinels=proof['limits']['producer_sentinels'];started=time.monotonic()
    receipt={'schema':'cupid.native-source-stage-replay.v1','status':'running','host':host,'commands':[],'child_receipts':{},
             'runner':identity(Path(__file__).read_bytes()),'proof':identity(proof_bytes),'producer_sentinels':sentinels,'checkpoint':proof['checkpoint']}
    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path,data in captured.items():
            if common.read(path)!=data:raise ValueError('Input changed during replay: '+str(path))
    def run(label,script,arguments):
        guard();before=time.monotonic();argv=[sys.executable,str(output/script),*map(str,arguments)]
        result=subprocess.run(argv,cwd=root,env={**os.environ,**sentinels},capture_output=True)
        for stream in ('stdout','stderr'):(output/(label+'-'+stream+'.bin')).write_bytes(getattr(result,stream))
        receipt['commands'].append({'label':label,'argv':argv,'exit_code':result.returncode,'seconds':time.monotonic()-before,'stdout':identity(result.stdout),'stderr':identity(result.stderr)})
        (output/'running.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
        if result.returncode!=0:raise ValueError(receipt['commands'][-1])
        guard()
    steps=[('root_build','build-root-regressions.py',[output/('root-products-'+host+'1')],'root-products'),
           ('source-root','run-source-root-contracts.py',[],'root-contracts'),('source-profile','run-source-observer-contracts.py',[],'source-contracts'),
           ('missing-unc','run-missing-unc-contracts.py',[],'missing-unc'),('resolved-parent','run-resolved-parent-contracts.py',[],'resolved-parent'),
           ('contracts','run-retained-contracts.py',[],'contracts'),('legacy','run-legacy-contracts.py',['1'],'legacy'),('alias','run-alias-regressions.py',[],'alias'),
           ('build','build-source-stage.py',[output/('stage-products-'+host+'1')],'stage-products'),('required','run-required-file-contracts.py',[],'file-contracts'),
           ('optional','run-optional-contracts.py',[],'optional-contracts'),('stage','run-source-stage.py',[],'stage-contracts'),
           ('capacity_build','build-stage-capacity.py',[],'stage-capacity-products'),('capacity','run-stage-capacity.py',[],'stage-capacity-contracts')]
    try:
        for label,script,arguments,_ in steps:run(label,script,arguments)
        groups={}
        for label,_,_,stem in steps:
            folder=output/(stem+'-'+host+'1');data=common.read(folder/'closed.json');record=json.loads(data)
            if record['status']!='pass':raise ValueError('A complete child failed: '+label)
            receipt['child_receipts'][label]=identity(data);captured[folder/'closed.json']=data;groups[label]=record
            if label in ('root_build','build'):
                if record['sources']!=proof['core_sources'] or record['objects']!=expected[label]['objects'] or record['checked_images']!=expected[label]['checked_images']:
                    raise ValueError('Complete checked build differs: '+label)
                for name,facts in record['objects'].items():
                    path=folder/'objects'/name;data=common.read(path)
                    if identity(data)!=facts:raise ValueError('Complete object differs: '+name)
                    captured[path]=data
                for role in ('native','checked'):
                    for name,facts in record[role+'_images'].items():
                        path=folder/(role+'-'+name+'.'+suffix);data=common.read(path)
                        if identity(data)!=facts:raise ValueError('Complete program differs: '+name)
                        captured[path]=data
            elif label=='capacity_build':
                if record['object']!=expected[label]['object'] or record['checked_image']!=expected[label]['checked_image']:raise ValueError('Complete capacity products differ')
                for name,facts in [('stage-capacity.o',record['object']),('native-stage-capacity.'+suffix,record['native_image']),('checked-stage-capacity.'+suffix,record['checked_image'])]:
                    path=folder/name;data=common.read(path)
                    if identity(data)!=facts:raise ValueError('Capacity product differs: '+name)
                    captured[path]=data
            else:
                if case_plan(record)!=case_plan(expected[label]):raise ValueError('Complete original case plan differs: '+label)
                if 'calls' in record and record['calls']!=len(record['records']):raise ValueError('An API call is missing: '+label)
                for row in record['records']:
                    if row.get('status','pass') not in ('pass','skip') or row.get('error') is not None:raise ValueError('A retained result differs: '+label)
                    if 'exit_code' in row and (row['exit_code']!=row.get('expected_exit',row.get('expected_exit_code',0)) or row['stderr']!=''):raise ValueError('A runtime result differs: '+label)
                    if 'timeout_seconds' in row and (row['timeout_seconds']!=60 or not 0<=row['seconds']<60):raise ValueError('A runtime deadline changed: '+label)
                    if not windows:
                        for key,value in (('address_space_bytes',33554432),('descriptor_limit',10240),('linux_address_space_bytes',33554432),('linux_descriptor_limit',10240)):
                            if key in row and row[key]!=value:raise ValueError('A runtime resource bound changed: '+label)
                    if label=='capacity' and (len(set(row['requests']))!=4096 or len(row['python_final_rejects'])!=4096 or sum(row['python_final_rejects'])!=(1 if row['mutation']=='last-leaf' else 0)):
                        raise ValueError('A complete capacity/Python oracle differs')
                for key in ('calls','positive_calls','negative_calls','selected_methods','passed_methods','declared_skips','test_bodies_changed','original_test_timeouts_changed','original_process_resource_limits_preserved'):
                    if key in expected[label] and record.get(key)!=expected[label][key]:raise ValueError('The retained scope differs: '+label+':'+key)
            if label in ('root_build','build','capacity_build'):
                for command in record['commands']:
                    if command['exit_code']!=0:raise ValueError('A build command failed')
                    if command['label'].startswith('strict-') and not all(flag in command['argv'] for flag in ('--require-known','--require-local-targets','--require-code-anchors')):raise ValueError('A strict flag is missing')
                    for stream in ('stdout','stderr'):
                        path=folder/(command['label']+'-'+stream+'.bin');data=common.read(path)
                        if identity(data)!=command[stream] or data!=b'':raise ValueError('A command stream differs')
                        captured[path]=data
        current=sum(groups[label]['calls'] for label in ('source-root','source-profile','missing-unc','resolved-parent','contracts','required','optional','stage','capacity'))
        if current!=(980 if windows else 1074):raise ValueError('The complete API scope differs')
        guard();receipt.update(status='pass',complete_api_calls=current,new_stage_api_calls=groups['stage']['calls']+groups['capacity']['calls'],
                              legacy_passed_methods=groups['legacy']['passed_methods'],legacy_declared_skips=groups['legacy']['declared_skips'],alias_passed_methods=24,
                              controlled_files=len(captured),controlled_file_identities={str(path):identity(data) for path,data in captured.items()})
    except BaseException as error:receipt.update(status='fail',error=repr(error));traceback.print_exc()
    receipt['seconds']=time.monotonic()-started
    (output/'closed.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({key:receipt.get(key) for key in ('status','host','seconds','complete_api_calls','new_stage_api_calls','error')}))
    return 0 if receipt['status']=='pass' else 1

if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as error:
        if created_output is not None:
            (created_output/'closed.json').write_text(json.dumps({'schema':'cupid.native-source-stage-replay-preflight.v1','status':'fail','error':repr(error)},indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
        raise
