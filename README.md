# zlib

`zlib` 把系统 zlib 的 **deflate / inflate** 最小面映射成 CT，全部能力挂在两个类型宿主上：
`z_stream *`（状态机，完全控制）与 `char *`（一次性文本）。零成本：每个方法在调用点展开成
普通 C 调用，没有 wrapper、没有 vtable、没有自定义句柄类型。

## 1. 定位与边界

**能做**

- 状态机：`deflateInit` / `deflate` / `deflateEnd` / `inflateInit` / `inflate` / `inflateEnd`。
- 一步式缓冲：`deflateBuffer` / `inflateBuffer`，装好四个字段再调用一次。
- 一次性文本：`text.deflate(out, cap)` / `in.inflate(inLen, out, cap)` / `text.deflateBound()`。
- 与 `<zlib.h>` 的 `Z_*` 常量并用；macOS / Linux 链接系统 `libz`。

**不做**

- 不是 zlib 全量绑定：只覆盖上面 6 个状态机函数与两类便捷入口。
- 不覆盖 gzip（`deflateInit2` 的 `windowBits`）、`compress` / `uncompress`、
  `crc32` / `adler32`、字典、`deflateSetHeader`、`inflateBack` 等其余 API。
- 所有方法都只调用一次，**不循环**；多轮推进要自己按 `avail_in` / `avail_out` 移动指针。
- 不翻译错误码：透传 zlib 的 `Z_*` 返回值，不提供错误字符串。
- 不做内存安全检查：原始指针与长度由调用者负责。
- 不支持 Windows；不 vendored 上游源码。

## 2. 依赖与安装

`ct.pkg` 的关键行：

```ini
name = zlib
version = 0.1.0
licence = Zlib
ldflags = -lz
requires = ct >= 0.19.1
```

- **`requires = ct >= 0.19.1`**：类型宿主写法（`for type z_stream *`）的实测版本；
  `0.17` / `0.18` 未做回归验证。
- **`ldflags = -lz`**：`ct install` 会把它折进打印的 `ct build` 命令。
- **无 `dep`**：不拉任何其它 CT 包；**无 `cflags`**：不需要额外宏定义。
- 包尚未写入 `ct-pkg/index`，当前用本地目录取包（已实测）：

```sh
ct get /path/to/ct-pkg/zlib
ct install
```

`ct get <目录>` 会把目录复制进 `vendor/zlib`、计算摘要写入 `ct.lock`，并在消费者
`ct.pkg` 末尾追加一行 `dep`；`ct install` 离线校验 `vendor/` 与 `ct.lock`，然后打印一条
可直接执行的 `ct build` 命令。索引收录后改为 `ct get zlib` / `ct get zlib@^0.1.0`。
`ct verify` 可离线复核摘要，实测输出 `verified 1 package(s)`。

**平台**

- **macOS（已实测）**：SDK 自带 `<zlib.h>` 与 `libz`，不需要 Homebrew，也不需要 `CPATH`。
- **Linux（未验证）**：安装 `zlib1g-dev`（Debian/Ubuntu）或 `zlib-devel`（Fedora），
  `-lz` 由系统链接器解析。本包未在 Linux 上实际运行。
- **Windows**：不支持。
- 只有头文件位于非默认前缀时，才需要 `CPATH` / `LIBRARY_PATH`，或直接加
  `--cflags '-I/your/prefix/include'` 与 `--ldflags '-L/your/prefix/lib -lz'`。

## 3. 快速开始

工程结构：`ct.pkg` 与 `src/main.ct` 两个文件，`vendor/` 由 `ct get` 生成。

`ct.pkg`：

```ini
name = zlib-demo
version = 0.1.0
```

`src/main.ct`（完整文件，一次性文本往返）：

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    char packed[64];
    char back[64];
    char *text;
    char *in;
    int n;
    int r;

    text = "hello hello hello";
    r = text.deflate(packed, 64);
    if (r < 0) { printf("deflate=%d\n", r); return 1; }
    n = r;
    printf("deflate=%d\n", n);

    in = packed;
    r = in.inflate(n, back, 64);
    if (r < 0) { printf("inflate=%d\n", r); return 1; }
    back[r] = 0;
    printf("inflate=%d text=%s\n", r, back);
    return 0;
}
```

命令与真实输出（`ct v0.19.1`、macOS arm64、zlib 1.2.12）：

```sh
ct get /path/to/ct-pkg/zlib
ct install
ct build -I vendor/zlib --cflags '-Wall -Wextra' --ldflags '-lz' src/main.ct -o build
./build
```

```
vendored 1 package(s)
ct build -I vendor/zlib --ldflags '-lz' <unit>.ct -o build
'cc' -std=c99 -Wall -Wextra -I 'vendor/zlib/zlib' '.ctbuild/main.c' -lz '-o' 'build'
deflate=16
inflate=17 text=hello hello hello
```

读法：`deflate` 返回压缩后的字节数（这段固定输入是 16 字节，**换 zlib 版本会变，验收看往返
一致性，不要断言这个数字**）；`inflate` 需要这个长度，返回 17；`back` 不自动补 NUL。

## 4. API 全表

### 4.1 宿主 `z_stream *`：状态机（6 个）

| 方法 | 签名 | 失败返回 |
| --- | --- | --- |
| `deflateInit` | `int deflateInit(int level)` | 透传 `Z_OK` / `Z_MEM_ERROR` 等 zlib 码 |
| `deflate` | `int deflate(int flush)` | `Z_OK` / `Z_STREAM_END` / `Z_BUF_ERROR` / `Z_STREAM_ERROR` |
| `deflateEnd` | `int deflateEnd()` | 成功 `Z_OK`，重复调用返回错误码 |
| `inflateInit` | `int inflateInit()` | 透传 zlib 码 |
| `inflate` | `int inflate(int flush)` | 同 `deflate`，另有 `Z_DATA_ERROR` |
| `inflateEnd` | `int inflateEnd()` | 成功 `Z_OK`，重复调用返回错误码 |

- 全部直接展开成 C 调用；`deflateInit` / `inflateInit` 经 `zlib.shim.h` 补
  `ZLIB_VERSION` 与 `sizeof(z_stream)`。
- 调用者负责 `memset` 清零、设置四个缓冲字段、按剩余量推进。
- 接收者必须是指针变量（`zp = &zs; zp->deflate(...)`），按值声明不会自动取址。

### 4.2 宿主 `z_stream *`：`Buffer`（2 个）

| 方法 | 签名 | 失败返回 |
| --- | --- | --- |
| `deflateBuffer` | `int deflateBuffer(const char *in, int inLen, char *out, int outCap, int flush)` | 参数非法 `Z_STREAM_ERROR`，其余透传 |
| `inflateBuffer` | `int inflateBuffer(const char *in, int inLen, char *out, int outCap, int flush)` | 同上 |

- 语义：装好 `next_in` / `avail_in` / `next_out` / `avail_out`，**一次**调用
  `deflate` / `inflate`，原样返回状态码。
- 每次调用都重置四个计数器，不保留上一轮剩余量；多轮请直接用状态机方法。
- 空流、负长度、`in == 0 && inLen > 0`、`out == 0 && outCap > 0` 都返回 `Z_STREAM_ERROR`。

### 4.3 宿主 `char *`：一次性文本（3 个）

| 方法 | 签名 | 失败返回 |
| --- | --- | --- |
| `deflate` | `int deflate(char *out, int cap)` | 成功为压缩字节数；失败为负 zlib 码；缓冲不够 `-5` |
| `inflate` | `int inflate(int inLen, char *out, int cap)` | 成功为解压字节数；失败为负 zlib 码；缓冲不够/输入截断 `-5` |
| `deflateBound` | `int deflateBound()` | 参数非法 `-2`；否则为 `compressBound(strlen(s))` |

- `deflate` 的输入长度取 `strlen`，压缩级别固定 `Z_DEFAULT_COMPRESSION`；含 NUL 的二进制
  必须走 `z_stream *`。
- `inflate` 必须显式给 `inLen`，因为压缩输入是二进制。
- 只写一份 `char *`，没有 `const char *` 孪生宿主（0.20.0 会折叠 const，两份同名即歧义）。
- 接收者必须是变量：`"hello".deflate(...)` 在 `v0.19.1` 编译失败，先赋给 `char *`。

## 5. 从零到能干活

### 5.1 deflate / inflate 是一台有状态的流机器

`z_stream` 记录一台压缩或解压机器的全部状态。一次「压缩一段内存」的固定步骤：

1. `memset` 清零，`deflateInit` / `inflateInit` 建立状态；
2. 设置 `next_in` / `avail_in` / `next_out` / `avail_out`；
3. 调 `deflate` / `inflate`，可能一次跑完，也可能因输出缓冲不够而停下；
4. 读 `total_out` 等计数；
5. `deflateEnd` / `inflateEnd` 释放内部状态。

关键计数器：

| 字段 | 方向 | 含义 |
| --- | --- | --- |
| `next_in` / `avail_in` | 输入 | 下一段待消费字节的位置与剩余数；调用后 `avail_in` 是尚未消费的数量 |
| `next_out` / `avail_out` | 输出 | 下一段可写位置与剩余空间；调用后 `avail_out` 是还剩多少没写 |
| `total_in` / `total_out` | 统计 | 自 init 以来累计处理的字节数，`unsigned long` |

缓冲增长的两条路：

- **一次性**：给足输出缓冲，flush 用 `Z_FINISH`，正常一次调用返回 `Z_STREAM_END`。
- **输出不够**：`deflate` 返回 `Z_OK` 且 `avail_out == 0`。换更大缓冲、保持同一个流继续，
  或改用 `char *` 宿主（缓冲不够统一报 `-5`）。

### 5.2 状态机全流程

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    z_stream d;
    z_stream *dp;
    z_stream i;
    z_stream *ip;
    unsigned char packed[128];
    unsigned char back[128];
    char *text;
    int r;

    text = "hello hello hello";

    memset(&d, 0, sizeof(d));
    dp = &d;
    r = dp->deflateInit(6);
    if (r != Z_OK) { printf("deflate_init=%d\n", r); return 1; }
    dp->next_in = (unsigned char *)text;
    dp->avail_in = 17;
    dp->next_out = packed;
    dp->avail_out = 128;
    r = dp->deflate(Z_FINISH);
    printf("deflate=%d packed=%d total_in=%d\n", r, (int)d.total_out, (int)d.total_in);
    dp->deflateEnd();

    memset(&i, 0, sizeof(i));
    ip = &i;
    r = ip->inflateInit();
    if (r != Z_OK) { printf("inflate_init=%d\n", r); return 1; }
    ip->next_in = packed;
    ip->avail_in = 16;
    ip->next_out = back;
    ip->avail_out = 128;
    r = ip->inflate(Z_FINISH);
    printf("inflate=%d out=%d text=%.17s\n", r, (int)i.total_out, (char *)back);
    ip->inflateEnd();
    return 0;
}
```

真实输出：

```
deflate=1 packed=16 total_in=17
inflate=1 out=17 text=hello hello hello
```

`deflate=1` / `inflate=1` 都是 `Z_STREAM_END`，是一次性 `Z_FINISH` 的正常结果；
`Z_OK` 是 `0`。`total_out` 要在 `*End` 之前读。

### 5.3 `Buffer`：一次装好四个字段

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    z_stream zs;
    z_stream *zp;
    char packed[64];
    char back[64];
    char *text;
    int n;
    int r;

    text = "hello hello hello";
    memset(&zs, 0, sizeof(zs));
    zp = &zs;

    r = zp->deflateInit(6);
    if (r != Z_OK) return 1;
    r = zp->deflateBuffer(text, 17, packed, 64, Z_FINISH);
    n = (int)zs.total_out;
    printf("deflate=%d packed=%d\n", r, n);
    zp->deflateEnd();

    memset(&zs, 0, sizeof(zs));
    r = zp->inflateInit();
    if (r != Z_OK) return 1;
    r = zp->inflateBuffer(packed, n, back, 64, Z_FINISH);
    back[(int)zs.total_out] = 0;
    printf("inflate=%d text=%s\n", r, back);
    zp->inflateEnd();
    return 0;
}
```

真实输出：

```
deflate=1 packed=16
inflate=1 text=hello hello hello
```

### 5.4 状态码

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `Z_OK` | `0` | 成功，但流未必结束 |
| `Z_STREAM_END` | `1` | 一次性 `Z_FINISH` 完成（deflate）或输入已完整解出（inflate） |
| `Z_STREAM_ERROR` | `-2` | 状态/参数非法 |
| `Z_DATA_ERROR` | `-3` | 输入数据损坏（inflate） |
| `Z_MEM_ERROR` | `-4` | 内存分配失败 |
| `Z_BUF_ERROR` | `-5` | 缓冲不足或输入截断（不是致命错误） |

这些宏来自 `<zlib.h>`，由 C 预处理器展开；包不重复导出，也不提供错误字符串。

### 5.5 两个宿主怎么选

| 场景 | 用哪个宿主 |
| --- | --- |
| 一整段文本、一次压完 | `char *`：`text.deflate(...)` + `in.inflate(...)`（`in = packed`） |
| 含 NUL 的二进制 | `z_stream *`：`deflateBuffer` / `inflateBuffer` |
| 要读 `total_in` / `total_out` | `z_stream *`：直接读字段 |
| 分块喂入、输出缓冲不够要继续 | `z_stream *`：手动设四个字段后调 `deflate` / `inflate` |

两条接收者规则（`v0.19.1` 实测）：

- 接收者必须是变量、元素、调用或字段链，**字面量不行**：
  `"hello".deflate(...)` 报
  `error: method receiver must be a variable, an element, a call or a field chain`。
- 宿主是 `z_stream *` 时，接收者必须是指针变量，按值声明的 `z_stream` 不会自动取址：
  `zs.deflateInit(6)` 报 `error: no method 'deflateInit' for type 'z_stream'`。
  写 `zp = &zs; zp->deflateInit(6)`。

## 6. 常见配方

以下片段都已用 `ct build --cflags '-Wall -Wextra' --ldflags '-lz'` 编译并实际运行。

### 配方 1：分块喂入输入

前一块用 `Z_NO_FLUSH`，最后一块用 `Z_FINISH`：

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    z_stream d;
    z_stream *dp;
    z_stream i;
    z_stream *ip;
    unsigned char packed[128];
    unsigned char back[128];
    char *text;
    int n;
    int r;

    text = "hello hello hello";

    memset(&d, 0, sizeof(d));
    dp = &d;
    r = dp->deflateInit(6);
    if (r != Z_OK) { printf("deflate_init=%d\n", r); return 1; }
    dp->next_out = packed;
    dp->avail_out = 128;

    dp->next_in = (unsigned char *)text;
    dp->avail_in = 8;
    r = dp->deflate(Z_NO_FLUSH);
    printf("chunk1=%d avail_out=%u\n", r, dp->avail_out);

    dp->next_in = (unsigned char *)text + 8;
    dp->avail_in = 9;
    r = dp->deflate(Z_FINISH);
    printf("chunk2=%d avail_out=%u\n", r, dp->avail_out);

    n = (int)d.total_out;
    printf("packed=%d\n", n);
    dp->deflateEnd();

    memset(&i, 0, sizeof(i));
    ip = &i;
    r = ip->inflateInit();
    if (r != Z_OK) { printf("inflate_init=%d\n", r); return 1; }
    ip->next_in = packed;
    ip->avail_in = (unsigned int)n;
    ip->next_out = back;
    ip->avail_out = 128;
    r = ip->inflate(Z_FINISH);
    printf("inflate=%d out=%d\n", r, (int)i.total_out);
    back[(int)i.total_out] = 0;
    ip->inflateEnd();
    printf("text=%.17s\n", (char *)back);
    return 0;
}
```

真实输出：

```
chunk1=0 avail_out=126
chunk2=1 avail_out=112
packed=16
inflate=1 out=17
text=hello hello hello
```

要点：第一块后 `avail_out` 从 128 降到 126（已写出 2 字节头），此时返回 `Z_OK`；
最后一块 `Z_FINISH` 才返回 `Z_STREAM_END`。`next_in` 要指向尚未消费的字节。

### 配方 2：判读「输出缓冲不够」与「输入被截断」

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    char packed[8];
    char back[64];
    char *text;
    char *in;
    int r;

    text = "hello hello hello";

    r = text.deflate(packed, 8);
    printf("deflate_small=%d\n", r);

    in = packed;
    r = in.inflate(2, back, 64);
    printf("inflate_truncated=%d\n", r);

    printf("bound=%d\n", text.deflateBound());
    return 0;
}
```

真实输出：

```
deflate_small=-5
inflate_truncated=-5
bound=30
```

- `-5` 是 `Z_BUF_ERROR`：8 字节装不下完整结果；`char *` 宿主把「`Z_OK` + `avail_out == 0`」
  归一到 `-5`，调用方只需判负。
- `inflate_truncated=-5`：只喂了 2 字节头，输入不完整。
- `bound=30` 是 `compressBound(17)`，拿它给 `deflate` 的 `cap` 兜底。

### 配方 3：反复使用同一个 `z_stream`

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    z_stream zs;
    z_stream *zp;
    char out[64];
    int round;
    int r;

    zp = &zs;
    for (round = 1; round <= 2; round++) {
        memset(&zs, 0, sizeof(zs));
        r = zp->deflateInit(Z_DEFAULT_COMPRESSION);
        if (r != Z_OK) { printf("init_%d=%d\n", round, r); return 1; }
        r = zp->deflateBuffer("abc", 3, out, 64, Z_FINISH);
        printf("round%d_run=%d out=%d\n", round, r, (int)zs.total_out);
        printf("round%d_end=%d\n", round, zp->deflateEnd());
    }
    return 0;
}
```

真实输出：

```
round1_run=1 out=11
round1_end=0
round2_run=1 out=11
round2_end=0
```

要点：每轮 init 前必须重新 `memset`；`deflateEnd` 不是幂等 close，不要在同一个流上调用两次。

### 配方 4：二进制数据（含 NUL）

```ct
#include <stdio.h>
#include <string.h>
#include <zlib.h>

import zlib::zlib;

int main(void)
{
    z_stream zs;
    z_stream *zp;
    char raw[4];
    char packed[64];
    char back[8];
    int n;
    int r;

    raw[0] = 0;
    raw[1] = 1;
    raw[2] = 2;
    raw[3] = 0;

    memset(&zs, 0, sizeof(zs));
    zp = &zs;
    r = zp->deflateInit(6);
    if (r != Z_OK) return 1;
    r = zp->deflateBuffer(raw, 4, packed, 64, Z_FINISH);
    n = (int)zs.total_out;
    zp->deflateEnd();
    printf("deflate=%d packed=%d\n", r, n);

    memset(&zs, 0, sizeof(zs));
    r = zp->inflateInit();
    if (r != Z_OK) return 1;
    r = zp->inflateBuffer(packed, n, back, 8, Z_FINISH);
    printf("inflate=%d out=%d\n", r, (int)zs.total_out);
    printf("same=%d\n", memcmp(raw, back, 4) == 0);
    zp->inflateEnd();
    return 0;
}
```

真实输出：

```
deflate=1 packed=12
inflate=1 out=4
same=1
```

## 7. 排错

### `ct build` 链接报 `undefined symbols`

```
"_deflateInit_", referenced from:
    _ct_zlib_deflate_init in demo-23372c.o
ld: symbol(s) not found for architecture arm64
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

原因：漏了 `-lz`。包内 `ldflags = -lz` 只在 `ct install` 打印的命令里生效；手工 `ct build`
要自己带 `--ldflags '-lz'`。

### `ct check src/main.ct` 报 `'import' is not supported in compile mode`

```
src/main.ct:5:1: error: 'import' is not supported in compile mode
```

`ct check` 是单单元模式，看不见 `import` 的树。带 import 的单元请用 `ct build`。

### 字面量作接收者

```
error: method receiver must be a variable, an element, a call or a field chain
```

`v0.19.1` 不接受 `"hello".deflate(...)`。先 `char *text = "hello";`，再 `text.deflate(...)`。
字面量接收者需要 `v0.20.0`，本包不依赖该写法。

### 按值接收者调 `z_stream *` 方法

```
error: no method 'deflateInit' for type 'z_stream'
```

宿主是 `z_stream *`，按值声明不会自动取址。写 `zp = &zs; zp->deflateInit(6)`。

### 第一次调用就崩溃或行为异常

- 忘了 `memset(&zs, 0, sizeof(zs))`：`zalloc` / `zfree` / `opaque` 是栈上垃圾值。
- init 后没设 `next_out` / `avail_out` 就调 `deflate`。
- `avail_in` 超过实际可读字节数。

### `deflateEnd` / `inflateEnd` 第二次调用返回错误

它们不是幂等 close。每个成功 init 配对一次 end；复用变量前先 `memset` 再 init。

### `ct get <目录>` 后 `ct verify` 摘要不稳定

`ct get` 对本地目录按整棵树算摘要，`.ctbuild/`、编译产物、`.DS_Store` 都会被算进去。
发布或取包前清理：

```sh
rm -rf .ctbuild build .DS_Store
```

### `ct install` 打印的 `<unit>.ct` 直接粘贴报错

`<unit>` 是模板占位符，且 `ct install` 不递归子目录。教程源码在 `src/main.ct`，手动替换：

```sh
ct build -I vendor/zlib --ldflags '-lz' src/main.ct -o build
```

### 包维护者：recipe 里写 `sizeof(z_stream)`

```
zlib.m.ct:10:91: error: map recipe names 'z_stream' but it is not a binding parameter
```

recipe 的 `sizeof` 只接受绑定参数，`sizeof z` 得到指针大小（8）而不是结构体大小。
宏初始化必须走 `zlib.shim.h` 的 `ct_zlib_deflate_init` / `ct_zlib_inflate_init`。

## 8. 限制与未支持

1. **`char *` 宿主的输入按 `strlen` 计长。** 含 NUL 的二进制必须用 `z_stream *`。
2. **`inflate` 必须显式给 `inLen`。** 压缩数据是二进制，不能 `strlen`。
3. **`char *` 宿主的错误是负值。** 缓冲不够/输入截断统一 `-5`，不区分 `Z_OK + avail_out == 0`。
4. **返回 `int`。** `deflate` / `inflate` / `deflateBound` 超过 2 GiB 会截断；大流用状态机
   直接读 `unsigned long total_out`。
5. **字面量接收者需 `v0.20.0`。** 当前先赋给变量。
6. **`z_stream` 不会自动取址。** 先取指针再调方法。
7. **init / end 必须配对。** 没有 `Drop`、没有幂等 close；忘记 end 就是泄漏。
8. **线程安全由调用者保证。** 一个线程一个 `z_stream`，不要并发共享。
9. **`Z_*` 常量不是包导出的。** 来自 `<zlib.h>`，不要在包里定义同名全局，会与宏冲突。
10. **只链接系统库。** 没有上游源码、没有 amalgamation；版本与 `ZLIB_VERSION` 由宿主决定。
11. **`ct check` 不是验收路径。** 带 `import` 的单元请用 `ct build`。
12. **未支持**：gzip、`compress` / `uncompress`、`crc32` / `adler32`、字典、
   `deflateSetHeader`、`inflateBack`、Windows。

### 方法名占用

- `z_stream *` 宿主只属于本包：`deflate` / `inflate` / `deflateEnd` 等名字在该宿主上
  不会与其它包冲突。
- `char *` 是公共宿主：本包占用 `deflate` / `inflate` / `deflateBound`，其它包不得在
  `char *` 上重复定义。已有的 `std::utf8::encode` / `decode`、`std::fs::readFile` 等
  与本包无关，但不要再新增同名方法。
- 本包自己的 C helper 全部带 `ct_zlib_` 前缀，避免与 zlib 的全局符号重名。

## 9. 版本与变更记录

当前版本 `0.1.0`，未打 tag、未写入 `ct-pkg/index`。许可：上游 zlib 为 Zlib 许可证，
包内 `.ct` 代码为 MIT，全文见 [LICENSE](LICENSE)。

| 版本 | 变更 |
| --- | --- |
| `0.1.0` | 初版：`z_stream *` 状态机 6 个方法 + `Buffer` 2 个 + `char *` 文本 3 个；`requires` 定为 `ct >= 0.19.1`；不包含自定义句柄层 |

探针与可行性记录（宏初始化三条路、`z_stream` 字段访问、类型宿主、接收者限制）见
[NOTES.probe.md](NOTES.probe.md)。
