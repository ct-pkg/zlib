/*
 * zlib.shim.h - the macro adapters and the whole-buffer helpers for zlib.m.ct.
 *
 * `deflateInit` / `inflateInit` are upstream macros:
 *
 *   deflateInit(strm, level)
 *       -> deflateInit_((strm), (level), ZLIB_VERSION, (int)sizeof(z_stream))
 *   inflateInit(strm)
 *       -> inflateInit_((strm), ZLIB_VERSION, (int)sizeof(z_stream))
 *
 * A .m.ct recipe forwards a fixed parameter list and its `sizeof` accepts only
 * binding parameters, so neither `ZLIB_VERSION` nor `sizeof(z_stream)` can be
 * written in the recipe. These two static inline adapters keep the expansion
 * in C, next to the header that defines it.
 *
 * The rest are the bodies behind the `char *` host and the one-call buffer
 * helpers: a `.m.ct` prologue accepts only `#include` / `#define`, so the
 * implementation has to live here. They are `static inline`, so each consumer
 * translation unit gets its own copy and there is no separate object to link.
 *
 * Return convention for the whole-buffer helpers: a non-negative byte count on
 * success, a negative zlib error code otherwise. `Z_BUF_ERROR` (that is, -5)
 * means the caller's output buffer was too small for the whole result.
 *
 * License: Zlib (see LICENSE).
 */
#ifndef CT_ZLIB_SHIM_H
#define CT_ZLIB_SHIM_H

#include <string.h>
#include <zlib.h>

static inline int ct_zlib_deflate_init(z_stream *strm, int level)
{
    return deflateInit_(strm, level, ZLIB_VERSION, (int)sizeof(z_stream));
}

static inline int ct_zlib_inflate_init(z_stream *strm)
{
    return inflateInit_(strm, ZLIB_VERSION, (int)sizeof(z_stream));
}

static inline int ct_zlib_deflate_buffer(z_stream *strm, const char *in, int inLen,
                                         char *out, int outCap, int flush)
{
    if (!strm) return Z_STREAM_ERROR;
    if (inLen < 0 || outCap < 0) return Z_STREAM_ERROR;
    if (!in && inLen > 0) return Z_STREAM_ERROR;
    if (!out && outCap > 0) return Z_STREAM_ERROR;

    strm->next_in = (unsigned char *)in;
    strm->avail_in = (unsigned int)inLen;
    strm->next_out = (unsigned char *)out;
    strm->avail_out = (unsigned int)outCap;
    return deflate(strm, flush);
}

static inline int ct_zlib_inflate_buffer(z_stream *strm, const char *in, int inLen,
                                         char *out, int outCap, int flush)
{
    if (!strm) return Z_STREAM_ERROR;
    if (inLen < 0 || outCap < 0) return Z_STREAM_ERROR;
    if (!in && inLen > 0) return Z_STREAM_ERROR;
    if (!out && outCap > 0) return Z_STREAM_ERROR;

    strm->next_in = (unsigned char *)in;
    strm->avail_in = (unsigned int)inLen;
    strm->next_out = (unsigned char *)out;
    strm->avail_out = (unsigned int)outCap;
    return inflate(strm, flush);
}

static inline int ct_zlib_deflate_text(const char *s, char *out, int cap)
{
    z_stream z;
    int r;
    int n;

    if (!s) return Z_STREAM_ERROR;
    if (cap < 0) return Z_STREAM_ERROR;
    if (!out && cap > 0) return Z_STREAM_ERROR;

    memset(&z, 0, sizeof(z));
    r = deflateInit_(&z, Z_DEFAULT_COMPRESSION, ZLIB_VERSION, (int)sizeof(z_stream));
    if (r != Z_OK) return r;

    z.next_in = (unsigned char *)s;
    z.avail_in = (unsigned int)strlen(s);
    z.next_out = (unsigned char *)out;
    z.avail_out = (unsigned int)cap;

    r = deflate(&z, Z_FINISH);
    n = (r == Z_STREAM_END) ? (int)z.total_out : (r < 0 ? r : Z_BUF_ERROR);
    deflateEnd(&z);
    return n;
}

static inline int ct_zlib_inflate_text(const char *s, int inLen, char *out, int cap)
{
    z_stream z;
    int r;
    int n;

    if (!s && inLen > 0) return Z_STREAM_ERROR;
    if (inLen < 0 || cap < 0) return Z_STREAM_ERROR;
    if (!out && cap > 0) return Z_STREAM_ERROR;

    memset(&z, 0, sizeof(z));
    r = inflateInit_(&z, ZLIB_VERSION, (int)sizeof(z_stream));
    if (r != Z_OK) return r;

    z.next_in = (unsigned char *)s;
    z.avail_in = (unsigned int)inLen;
    z.next_out = (unsigned char *)out;
    z.avail_out = (unsigned int)cap;

    r = inflate(&z, Z_FINISH);
    n = (r == Z_STREAM_END) ? (int)z.total_out : (r < 0 ? r : Z_BUF_ERROR);
    inflateEnd(&z);
    return n;
}

static inline int ct_zlib_deflate_bound(const char *s)
{
    if (!s) return Z_STREAM_ERROR;
    return (int)compressBound((uLong)strlen(s));
}

#endif /* CT_ZLIB_SHIM_H */
