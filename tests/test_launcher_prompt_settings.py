"""Exercise original launcher's actual INI/environment helpers without a UI."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_launcher_prompt_settings_roundtrip():
    source = '#include "' + (ROOT/'tools/launcher/launcher.c').as_posix() + '"\n' + r'''
#include <assert.h>
int main(int argc,char** argv) {
 assert(argc==2);
 MultiByteToWideChar(CP_UTF8,0,argv[1],-1,g_ini,MAXP);
 load_settings();assert(g_settings.prompts==0);
 assert(prompt_setting_index(L"invalid")==0);
 for(int i=0;i<5;++i) {
   g_settings.prompts=i;save_settings();g_settings.prompts=-1;load_settings();
   assert(g_settings.prompts==i);configure_prompt_environment();
   wchar_t value[40];
   GetEnvironmentVariableW(L"LAJUNGLA_PROMPTS",value,40);
   assert(!wcscmp(value,prompt_values[i]));
   GetEnvironmentVariableW(L"TJ_PROMPTS",value,40);
   assert(!wcscmp(value,i==0?L"0":L"1"));
 }
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='tj-launcher-prompts-') as directory:
        directory=Path(directory)
        (directory/'test.c').write_text(source,encoding='utf-8')
        subprocess.run(['C:/Program Files/LLVM/bin/clang.exe','-D_CRT_SECURE_NO_WARNINGS',
                        str(directory/'test.c'),'-luser32','-lgdi32','-o',str(directory/'test.exe')],check=True)
        subprocess.run([str(directory/'test.exe'),str(directory/'launcher.ini')],check=True)
