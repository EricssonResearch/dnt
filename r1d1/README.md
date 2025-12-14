# R1D1

A minimal model of the packet forwarding of R2DTWO with eth interfaces, used for benchmark reference.

Build:

```
cc -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wvla -Wc++-compat -Werror r1d1.c -o r1d1
```

Run:

```
./r1d1 [-poll|-thread] iface1 iface2
```

The mode is *poll* when not specified.

The `r1d1.ini` config does almost the same forwarding as r1d1, designed to run on *nb* of [quicktest](../test/quick/README.md).
