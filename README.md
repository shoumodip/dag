# Dag
Dynamic Array library generator for C.

## Quick Start
```console
$ ./build.sh
$ ./dag xs int
```

```c
// File: main.c

#include <stdio.h>

#define XS_IMPLEMENTATION
#include "xs.h"

int main(void)
{
    Xs xs = {0};
    for (int i = 0; i < 10; ++i) xs_push(&xs, i);
    for (int i = 0; i < xs.count; ++i) printf("%d\n", xs.data[i]);;
    xs_free(&xs);
}
```

## Notes
- [STB](https://github.com/nothings/stb)
- [Header only Libraries](https://github.com/nothings/stb/blob/master/docs/stb_howto.txt)
