# zlib 包可行性探针（Phase 2）

环境：macOS arm64 · `ct v0.19.1` · Apple CommandLineTools SDK · zlib 1.2.12（`sizeof(z_stream) = 112`）。

所有探针都写在 `/tmp/zprobe`（Phase 2）与 `/tmp/zprobe2`（类型宿主，追加于 2026-11-07），
下面是原始代码与真实输出。

---

## 0. 结论速览

| 探针 | 结论 |
| --- | --- |
| 宏初始化 | 三条路里**选 shim**：`.m.ct` recipe 表达不出 `sizeof(z_stream)`；`.m.ct` 拒绝 `#ct-raw`；普通 `.ct` 能直接调 `deflateInit_`，但零成本层的 recipe 做不到 |
| `z_stream` 按值 / 按字段 | 可用。局部变量、字段读写、函数指针字段置 0、`total_out` 读取都成立 |
| 常量宏 | 直接来自 `<zlib.h>`（经 `.m.ct` prologue 展开），不需要 `-D`，也不需要 shim 导出 |
| `-lz` | 可用（`cc ... -lz` 与 `ct build --ldflags '-lz'` 均通过） |
| 类型宿主 `z_stream *` | 成立：`map trait ... for type z_stream *` 可用，`zp->deflate(Z_FINISH)` 直接展开成 `deflate(zp, Z_FINISH)` |
| 同名方法分挂两宿主 | 成立：`deflate` 同时挂在 `z_stream *` 与 `char *` 上，按接收者类型解析，不歧义 |
| 接收者语法 | 字面量接收者被拒；按值接收者不会自动取址到指针宿主（§6.3） |

最终包**不使用** `#ct-raw`；shim 是包内的适配层，不是语言能力。

---

## 1. 宏式初始化：三条路

`deflateInit(strm, level)` 是宏，展开为
`deflateInit_(&strm, level, ZLIB_VERSION, sizeof(z_stream))`。

### 路线 1：`#ct-raw` 内联

把 `deflateInit_` 的调用塞进 `.m.ct` 的 raw 块：

```ct
/* /tmp/zprobe/pkgraw/zlib.m.ct */
#include <zlib.h>

#ct-raw
static inline int zt_di(z_stream *s, int l) { return deflateInit_(s, l, ZLIB_VERSION, (int)sizeof(z_stream)); }
#ct-endraw

namespace zlib;
/* … trait / map 略 … */
```

真实输出：

```
$ ct build app/main.ct -I pkgraw -o ./pkgraw/a --ldflags '-lz'
pkgraw/zlib.m.ct:3:1: error: a .m.ct unit allows only #include and #define in its prologue
ctc: 1 error(s) generated
```

**失败**：`.m.ct` 的白名单只有 `#include` / `#define` / `import` / `pub trait` / `map` /
`_Static_assert`（batch38 §3.5）。`#ct-raw` 在普通 `.ct` 里可用（`test/ast/65_raw/prog.ct`），
但**不是** `.m.ct` 的能力，所以不能作为零成本层的解法。

### 路线 2：shim 里包一层（采用）

```c
/* /tmp/zprobe/full/zlib.shim.h */
#include <zlib.h>

static inline int zt_deflate_init(z_stream *strm, int level)
{
    return deflateInit_(strm, level, ZLIB_VERSION, (int)sizeof(z_stream));
}

static inline int zt_inflate_init(z_stream *strm)
{
    return inflateInit_(strm, ZLIB_VERSION, (int)sizeof(z_stream));
}
```

```ct
/* /tmp/zprobe/full/zlib.m.ct */
#include <zlib.h>

namespace zlib;

pub trait Init {
    int deflate_init(z_stream *strm, int level);
    int inflate_init(z_stream *strm);
}

map trait Init for namespace zlib {
    deflate_init(strm, level) -> zt_deflate_init(strm, level);
    inflate_init(strm)        -> zt_inflate_init(strm);
}
```

真实输出（shim 被自动 `#include`，`cc` 行自动带 `-I`）：

```
$ ct build app/main.ct -I full -o ./full/demo --ldflags '-lz'
'cc' -std=c99 -I 'full' '.ctbuild/main.c' -lz '-o' './full/demo'
deflate r=1 out=16
inflate r=1 n=17 text=hello hello hello
```

**采用**。理由：宏、`ZLIB_VERSION`、`sizeof(z_stream)` 全部留在 shim 里，`.m.ct` 只写一次普通调用；
与 `bindings/posix-socket` 的既有 shim 约定一致。

### 路线 3：直接调下划线版本并传 `zlibVersion()`

普通 `.ct` 单元里确实可以：

```ct
/* /tmp/zprobe/direct.ct */
#include <zlib.h>

int main(void)
{
    z_stream zs;
    int r;
    zs.zalloc = 0;
    zs.zfree = 0;
    zs.opaque = 0;
    r = deflateInit_(&zs, Z_DEFAULT_COMPRESSION, zlibVersion(), sizeof(z_stream));
    return r == Z_OK ? 0 : 1;
}
```

```
$ ct build /tmp/zprobe/direct.ct -o /tmp/zprobe/direct --ldflags '-lz'
'cc' -std=c99 '.ctbuild/direct.c' -lz '-o' '/tmp/zprobe/direct'
$ ./direct; echo $?
0
```

**但放不进零成本层**——recipe 的 `sizeof` 只接受绑定参数：

```ct
map trait Init for namespace zlib {
    deflate_init(strm, level) -> deflateInit_(strm, level, ZLIB_VERSION, sizeof(z_stream));
}
```

```
./zlib.m.ct:10:91: error: map recipe names 'z_stream' but it is not a binding parameter
```

`sizeof strm` 会得到指针大小（8），不是 `z_stream` 的大小，不能替代。所以路线 3 只对
“手写 `.ct`”成立，对 `.m.ct` 零成本层不成立。

---

## 2. `z_stream` 结构体

> 本节探针当时用的是命名空间入口（`zlib::Codec.deflate`），该入口已被 §6 的类型宿主取代；
> 字段读写结论不变。

```ct
#include <zlib.h>
#include <stdio.h>

int main(void)
{
    printf("%s %ld\n", zlibVersion(), (long)sizeof(z_stream));
    return 0;
}
```

```
$ cc -Wall -Wextra -std=c99 /tmp/zp1.c -lz -o /tmp/zp1 && /tmp/zp1
1.2.12 112
```

字段探针（完整往返，走 shim 初始化）：

```ct
z_stream d;
d.zalloc = 0;
d.zfree = 0;
d.opaque = 0;
d.next_in = (unsigned char *)"hello hello hello";
d.avail_in = 17;
d.next_out = packed;
d.avail_out = 128;
/* … */
r = zlib::Codec.deflate(&d, Z_FINISH);
printf("deflate r=%d out=%d\n", r, (int)d.total_out);
```

```
deflate r=1 out=16
inflate r=1 n=17 text=hello hello hello
```

- `next_in` / `avail_in` / `next_out` / `avail_out` 可写，`total_out` 可读；
- `zalloc` / `zfree` / `opaque` 置 `0` 后库接受（`deflateInit_` 内部装默认分配器）；
- 结构体可按值作局部变量，指针 `&d` 可传给 trait 方法；
- 公共 `.h` 里不能按值暴露 `z_stream`（生成的 `.h` 不含 prologue），但本包不再需要公共句柄：
  宿主就是 `z_stream *`，消费者在自己的 `.ct` 里 `#include <zlib.h>` 后按值声明并取地址（§6）。

---

## 3. 常量宏

`Z_OK` / `Z_STREAM_END` / `Z_NO_FLUSH` / `Z_FINISH` 全部直接来自 `<zlib.h>`：

```ct
r = zlib::Init.deflate_init(&d, 6);
if (r != Z_OK) { /* … */ }
r = zlib::Codec.deflate(&d, Z_FINISH);
```

编译运行通过（见 §1 路线 2 的输出）。CT 把全大写标识符原样发射，C 预处理器展开；
不需要 `-D`，也不需要 shim 导出常量。注意 `Z_DEFAULT_COMPRESSION` 是 `-1`，同样直接可用。

---

## 4. `-lz`

```
$ cc -Wall -Wextra -std=c99 /tmp/zp1.c -lz -o /tmp/zp1 && /tmp/zp1
1.2.12 112
$ ct build app/main.ct -I full -o ./full/demo --ldflags '-lz'
'cc' -std=c99 -I 'full' '.ctbuild/main.c' -lz '-o' './full/demo'
```

macOS 的 SDK 头文件在 `$(xcrun --show-sdk-path)/usr/include/zlib.h`（`/usr/include` 下没有）；
链接用 `-lz`，系统自带 `libz.1.2.12.dylib`。

---

## 5. 需要上报的能力缺口

1. `.m.ct` 不允许 `#ct-raw`（合理，白名单很窄），因此“宏展开”只能走 shim。
2. recipe 的 `sizeof` 只接受绑定参数（`sizeof p`），不能写 `sizeof <C 类型>`；宏初始化里的
   `sizeof(z_stream)` 无法在 recipe 内表达。
3. 字面量不能作方法接收者：`"hello".deflate(...)` 报
   `error: method receiver must be a variable, an element, a call or a field chain`。
   计划在 `v0.20.0` 支持；当前包所有示例都先赋给 `char *` 变量。
4. `.h.ct` 的公共头不带 prologue，公共 struct 不能暴露 `z_stream`。本包不再自定义句柄，
   改为把宿主定成 `z_stream *`：消费者自己的 `.ct` 有 prologue，可以按值声明再取地址。

前三条是当前包模型的边界；包用 shim + 类型宿主绕开，没有改编译器。

---

## 6. 类型宿主探针（OO 化追加）

目的：确认能否把方法直接挂到 `z_stream *` 与 `char *`，以及接收者语法边界。
探针代码在 `/tmp/zprobe2`（`zlab/zlab.m.ct`、`zlab/zlab.shim.h`、`app/main.ct`）。

### 6.1 `map trait ... for type z_stream *`

```ct
pub trait Zs {
    int deflateInit(int level);
    int deflate(int flush);
    int inflate(int flush);
}

map trait Zs for type z_stream * {
    deflateInit(z, level) -> ct_probe_init(z, level);
    deflate(z, flush)     -> deflate(z, flush);
    inflate(z, flush)     -> inflate(z, flush);
}
```

```ct
z_stream zs;
z_stream *zp;
zp = &zs;
zp->deflateInit(6);
/* … 设置 next_in / avail_in / next_out / avail_out … */
printf("method_deflate=%d\n", zp->deflate(Z_FINISH));
```

```
'cc' -std=c99 -Wall -Wextra -I '/tmp/zprobe2/zlab' '.ctbuild/main.c' -lz '-o' '...'
pack=16
method_deflate=1
```

**成立**：`z_stream` 的定义来自 `.m.ct` 的 `#include <zlib.h>`，宿主可以直接写成
`z_stream *`；方法展开成普通 C 调用，零成本。

### 6.2 同名方法分挂两个宿主

同一单元里：

```ct
map trait Zs for type z_stream * {
    deflate(z, flush) -> deflate(z, flush);
}

map trait Text for type char * {
    deflate(s, out, cap) -> ct_probe_pack(s, out, cap);
}
```

```ct
n = s.deflate(packed, 64);        /* char * 宿主 */
r = zp->deflate(Z_FINISH);        /* z_stream * 宿主 */
```

```
pack=16
method_deflate=1
```

**成立**：接收者类型不同，调用点按宿主解析；参数个数不同也不歧义。

### 6.3 接收者语法

| 写法 | 结果 |
| --- | --- |
| `"hello".deflate(...)` | 失败：`error: method receiver must be a variable, an element, a call or a field chain` |
| `zs.deflateInit(6)`（`zs` 是按值 `z_stream`） | 失败：`error: no method 'deflateInit' for type 'z_stream'` |
| `char *s = "hello"; s.deflate(...)` | 通过 |
| `zp = &zs; zp->deflate(...)` | 通过 |

结论：字面量接收者需要 `v0.20.0`；指针宿主必须用指针变量。

### 6.4 只写一份 `char *`

`std` 的 `char *` 宿主会做顶层 const 折叠。不要再写 `for type const char *` 的孪生宿主：
`v0.20.0` 折叠后两份同名方法会歧义，因此本包只保留 `map trait Text for type char *` 一份。
