"""Save transactions use synthetic temporary slots only. Author: matymeatboy."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT.parent/'ps3recomp'
CLANG = shutil.which('clang') or r'C:\Program Files\LLVM\bin\clang.exe'
CLANGXX = shutil.which('clang++') or r'C:\Program Files\LLVM\bin\clang++.exe'


def augmented_harness():
    source = (ROOT/'tests/savedata_guest_io/main.c').read_text(encoding='utf-8')
    source = source.replace('#include <stdio.h>', '''#include <stdio.h>
int inject_short_write;
size_t tj_test_savedata_fwrite(const void* p,size_t size,size_t count,FILE* stream){
    if(inject_short_write&&count){inject_short_write=0;return fwrite(p,size,count-1,stream);}
    return fwrite(p,size,count,stream);
}'''.replace('\n+', '\n'))
    source = source.replace('static int s_is_save;', 'static int s_is_save;\nstatic int fail_after_write;')
    source = source.replace('    if (opd == FILE_OPD) {', '''    if (opd == FILE_OPD) {
        if (fail_after_write && s_file_phase) {
            write_be32(cb_ea, CELL_SAVEDATA_CBRESULT_ERR_FAILURE);
            return;
        }''')
    marker = '    check_native_callbacks(data_path);'
    extra = '''    /* Both ordinary overwrite and reCreate must roll back a later failure. */
    s_is_save=1;s_expect_new=0;fail_after_write=0;
    memset(vm_base+SAVE_BUF_EA,0xCC,sizeof(s_payload));
    s_recreate_mode=CELL_SAVEDATA_RECREATE_NO_NOBROKEN;s_file_phase=0;inject_short_write=1;
    rc=cellSaveDataAutoSave2(0,(const char*)(uintptr_t)DIR_EA,0,
        (CellSaveDataSetBuf*)(uintptr_t)SETBUF_EA,
        (CellSaveDataStatCallback)(uintptr_t)STAT_OPD,
        (CellSaveDataFileCallback)(uintptr_t)FILE_OPD,0,(void*)(uintptr_t)0x12345678u);
    expect_u32("short write returns access error",(uint32_t)rc,(uint32_t)CELL_SAVEDATA_ERROR_ACCESS_ERROR);
    expect_u32("short write preserves original progress",file_equals(data_path,s_payload,sizeof(s_payload)),1);
    fail_after_write=1;
    for(int recreate=0;recreate<2;recreate++) {
        s_recreate_mode=recreate?CELL_SAVEDATA_RECREATE_YES:CELL_SAVEDATA_RECREATE_NO_NOBROKEN;
        s_file_phase=0;
        rc=cellSaveDataAutoSave2(0,(const char*)(uintptr_t)DIR_EA,0,
            (CellSaveDataSetBuf*)(uintptr_t)SETBUF_EA,
            (CellSaveDataStatCallback)(uintptr_t)STAT_OPD,
            (CellSaveDataFileCallback)(uintptr_t)FILE_OPD,0,(void*)(uintptr_t)0x12345678u);
        expect_u32("late failing callback result",(uint32_t)rc,(uint32_t)CELL_SAVEDATA_ERROR_CBRESULT);
        expect_u32("late failing callback preserves original progress",file_equals(data_path,s_payload,sizeof(s_payload)),1);
    }
    fail_after_write=0;s_recreate_mode=CELL_SAVEDATA_RECREATE_NO_NOBROKEN;
    memcpy(vm_base+SAVE_BUF_EA,s_payload,sizeof(s_payload));
'''
    return source.replace(marker, extra+marker)


def compile_runtime_harness(tmp_path, atomic=True):
    source = (RUNTIME/'libs/system/cellSaveData.c').read_text(encoding='utf-8')
    runtime_c = tmp_path/'libs/system/cellSaveData.c'
    runtime_c.parent.mkdir(parents=True)
    source = source.replace('#include <time.h>', '#include <time.h>\nextern size_t tj_test_savedata_fwrite(const void*,size_t,size_t,FILE*);\n#define fwrite tj_test_savedata_fwrite')
    runtime_c.write_text(source, encoding='utf-8')
    if atomic and 'TJ_ATOMIC_BEGIN' not in source:
        subprocess.run(['git', 'apply', str(ROOT/'patches/ps3recomp/tj-savedata-atomic.patch')], cwd=tmp_path, check=True)
    harness = tmp_path/'harness.c'
    harness.write_text(augmented_harness(), encoding='utf-8')
    include = []
    for directory in ('include', 'libs/system', 'runtime/ppu', 'runtime/memory'):
        include += ['-I', str(RUNTIME/directory)]
    runtime_object, harness_object = tmp_path/'runtime.obj', tmp_path/'harness.obj'
    flags = ['-DTJ_SAVEDATA_TRANSACTION=1'] if atomic else []
    subprocess.run([CLANG, '-std=c11', *include, *flags, '-c', str(runtime_c), '-o', str(runtime_object)], check=True, capture_output=True, text=True)
    subprocess.run([CLANG, '-std=c11', *include, '-c', str(harness), '-o', str(harness_object)], check=True, capture_output=True, text=True)
    executable = tmp_path/'savedata.exe'
    sources = [str(ROOT/'src/tj_savedata_transaction.cpp')] if atomic else []
    subprocess.run([CLANGXX, '-std=c++17', *sources, str(runtime_object), str(harness_object), '-o', str(executable)], check=True, capture_output=True, text=True)
    return executable


def test_actual_guest_save_late_failure_and_recreate_rollback(tmp_path):
    executable = compile_runtime_harness(tmp_path)
    subprocess.run([str(executable)], cwd=tmp_path, check=True, capture_output=True, text=True)
    subprocess.run([str(executable), 'userdata-rebinding'], cwd=tmp_path, check=True, capture_output=True, text=True)


HELPER_TEST = r'''
#include "tj_savedata_transaction.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
extern "C" void tj_savedata_transaction_test_publish_failure(int);
namespace fs=std::filesystem;
static void put(const fs::path& p,const char* data){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<data;assert(f.good());}
static std::string get(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
static fs::path route(const fs::path& p){char path[4096];auto s=p.u8string();std::strcpy(path,s.c_str());assert(tj_savedata_transaction_route(path,sizeof(path)));return fs::u8path(path);}
int main(int argc,char** argv){assert(argc==3);auto original=fs::absolute(fs::u8path(argv[1]));std::string mode=argv[2];
 auto work=original.parent_path()/".savedata-recovery"/original.filename();
 if(mode=="reject"){assert(!tj_savedata_transaction_begin(original.u8string().c_str(),1));return 0;}
 if(mode=="recover"){assert(tj_savedata_transaction_begin(original.u8string().c_str(),0));assert(route(original)==original);assert(get(original/"DATA.BIN")=="committed");assert(tj_savedata_transaction_finish(1));return 0;}
 fs::create_directories(original);put(original/"DATA.BIN","committed");put(original/"PARAM.SFO","old metadata");put(original/"EXTRA.BIN","preserve me");
 assert(tj_savedata_transaction_begin(original.u8string().c_str(),1));auto pending=route(original);assert(pending!=original);
 if(mode=="interrupt"){
   put(pending/"DATA.BIN","partial candidate");fs::remove_all(work/"previous");fs::rename(original,work/"previous");std::_Exit(0);
 }
 bool second_writer=true;std::thread other([&]{second_writer=tj_savedata_transaction_begin(original.u8string().c_str(),1)!=0;});other.join();assert(!second_writer);
 put(pending/"DATA.BIN","partial candidate");put(pending/"PARAM.SFO","new metadata");fs::remove(pending/"EXTRA.BIN");
 assert(get(original/"DATA.BIN")=="committed");assert(tj_savedata_transaction_finish(0));
 assert(get(original/"DATA.BIN")=="committed"&&get(original/"PARAM.SFO")=="old metadata"&&get(original/"EXTRA.BIN")=="preserve me");
 assert(tj_savedata_transaction_begin(original.u8string().c_str(),1));pending=route(original);
 fs::remove_all(pending);put(pending/"DATA.BIN","recreated");put(pending/"PARAM.SFO","recreated metadata");
 tj_savedata_transaction_test_publish_failure(1);assert(!tj_savedata_transaction_finish(1));tj_savedata_transaction_test_publish_failure(0);
 assert(get(original/"DATA.BIN")=="committed"&&get(original/"EXTRA.BIN")=="preserve me");
 assert(tj_savedata_transaction_begin(original.u8string().c_str(),1));pending=route(original);put(pending/"DATA.BIN","new committed");assert(tj_savedata_transaction_finish(1));
 assert(get(original/"DATA.BIN")=="new committed"&&get(work/"previous/DATA.BIN")=="committed");
 auto fresh=original.parent_path()/"new-slot";assert(tj_savedata_transaction_begin(fresh.u8string().c_str(),1));assert(!fs::exists(route(fresh)));assert(tj_savedata_transaction_finish(0));assert(!fs::exists(fresh));
 return 0;}
'''


def test_commit_rename_failure_interruption_recovery_and_links(tmp_path):
    harness = tmp_path/'transaction.cpp'
    harness.write_text(HELPER_TEST)
    executable = tmp_path/'transaction.exe'
    subprocess.run([CLANGXX, '-std=c++17', '-DTJ_SAVEDATA_TRANSACTION_TEST=1',
                    '-I', str(ROOT/'src'), str(harness), str(ROOT/'src/tj_savedata_transaction.cpp'),
                    '-o', str(executable)], check=True, capture_output=True, text=True)
    slot = tmp_path/'saves/slot'
    subprocess.run([str(executable), str(slot), 'normal'], check=True)
    interrupted = tmp_path/'saves/interrupted'
    subprocess.run([str(executable), str(interrupted), 'interrupt'], check=True)
    assert not interrupted.exists()
    subprocess.run([str(executable), str(interrupted), 'recover'], check=True)
    assert (interrupted/'DATA.BIN').read_bytes() == b'committed'
    outside = tmp_path/'outside'
    outside.mkdir()
    (outside/'PRIVATE.BIN').write_bytes(b'untouched')
    link = slot/'external'
    if __import__('os').name == 'nt':
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(outside)], check=True, capture_output=True)
    else:
        link.symlink_to(outside, target_is_directory=True)
    try:
        subprocess.run([str(executable), str(slot), 'reject'], check=True)
        assert (outside/'PRIVATE.BIN').read_bytes() == b'untouched'
    finally:
        link.rmdir() if __import__('os').name == 'nt' else link.unlink()
