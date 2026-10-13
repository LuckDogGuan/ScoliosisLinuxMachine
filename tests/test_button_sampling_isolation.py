import pathlib,subprocess,tempfile
r=pathlib.Path(__file__).resolve().parents[1]
t=tempfile.mkdtemp(prefix='button-sampling-isolation-')
subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-O2','-Iinclude','-Ithird_party/cjson','-Ithird_party/lwbtn/include','tests/test_button_sampling_isolation.c','third_party/lwbtn/lwbtn.c','-o',t+'/check'],cwd=r,check=True)
subprocess.run([t+'/check'],check=True,timeout=5)
print('Evidence:',t)
