
default:
	cc -Wall -std=c99 -g inilint.c ../inifile.c ../hashmap.c -o inilint -lm
	cc -Wall -std=c99 -g jslint.c ../json.c ../hashmap.c -o jslint -lm

fuzz:
	AFL_USE_ASAN=1 afl-cc -Wall -std=c99 -g inilint.c ../inifile.c ../hashmap.c -o inilint -lm
	AFL_USE_ASAN=1 afl-cc -Wall -std=c99 -g jslint.c ../json.c ../hashmap.c -o jslint -lm
