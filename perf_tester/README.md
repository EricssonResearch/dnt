# perf_tester

A minimal model of the packet forwarding of DNT with eth interfaces, used for benchmark reference.

Build:

```
cc -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wvla -Wc++-compat -Werror perf_tester.c -o perf_tester
```

Run:

```
./perf_tester [-poll|-thread] iface1 iface2
```

The mode is *poll* when not specified.

The `perf_tester.ini` config does almost the same forwarding as perf_tester, designed to run on *nb* of [quicktest](../test/quick/README.md).
