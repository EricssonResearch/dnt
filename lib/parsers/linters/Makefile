
default:
	cc -Wall -std=c99 -g inilint.c ../inifile.c ../hashmap.c -o inilint -lm
	cc -Wall -std=c99 -g jslint.c ../json.c ../hashmap.c -o jslint -lm

# fuzzing example: AFL_AUTORESUME=1 AFL_SKIP_CPUFREQ=1 afl-fuzz -i afl-input/ -o afl-output/ ./jslint @@
fuzz:
	AFL_USE_ASAN=1 afl-cc -Wall -std=c99 -g inilint.c ../inifile.c ../hashmap.c -o inilint -lm
	AFL_USE_ASAN=1 afl-cc -Wall -std=c99 -g jslint.c ../json.c ../hashmap.c -o jslint -lm
