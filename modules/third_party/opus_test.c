/* ============================================================================
 * main.c — opus-1.3.1-lite Opus <-> PCM 转换测试程序（宏参数版）
 *
 * 设计：所有 Opus / PCM 参数在文件顶部用宏指定（或编译命令行覆盖），
 *       不解析命令行参数，main() 按宏配置执行固定测试流程。
 *
 * 依赖：仅 C 标准库 (stdio/stdlib/string/stdint/math) + 本目录 libopus 源码
 *       不依赖 libogg / opusfile / 任何平台 API
 *
 * 修改参数的两种方式：
 *   1) 直接改本文件顶部 CFG_* 宏，重新 make
 *   2) 编译时覆盖，不改文件：
 *      make EXTRA_CFLAGS="-DCFG_OPUS_RATE=8000 -DCFG_OPUS_FRAME_MS=40"
 *
 * 用法（输入/输出文件由命令行参数指定）：
 *   opuslite in.wav out.ogg    WAV -> Ogg Opus：任意采样率/位深/单双声道输入，
 *                              自动声道适配 + 线性重采样到 CFG_OPUS_RATE 后编码
 *   opuslite in.ogg out.wav    Ogg -> WAV：输出 CFG_OPUS_RATE/CFG_OPUS_CHANNELS/16-bit
 *                              （方向按文件头 RIFF/OggS 自动判断）
 *   opuslite --selftest x.ogg  内置正弦，完整回环并测 SNR
 *
 * 自实现组件（全部纯 C）：
 *   - WAV RIFF 解析、Ogg 页封装/解封装（RFC 3533，CRC-32 poly=0x04C11DB7）
 *   - OpusHead / OpusTags 头构建与解析（RFC 7845）
 *   - 线性插值重采样器、声道折叠/扩展
 *   - 回环 SNR 评测（延迟搜索对齐）
 *
 * Ogg 时间域说明（RFC 7845 §4/§5.1）：
 *   Ogg Opus 的 granule 与 pre-skip 固定以 48 kHz 样本计数，与编码器
 *   实际工作采样率无关，故按 OGG_SCALE = 48000 / CFG_OPUS_RATE 换算。
 * ============================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "opus.h"

/* ============================ 1. 可选参数（宏指定） ============================
 * Opus 编码器/解码器与 PCM 格式全部由以下宏决定 */
#ifndef CFG_OPUS_RATE
#define CFG_OPUS_RATE        16000   /* Opus 采样率: 8000/12000/16000/24000/48000 Hz */
#endif
#ifndef CFG_OPUS_FRAME_MS
#define CFG_OPUS_FRAME_MS    20      /* 帧长: 5/10/20/40/60 ms */
#endif
#ifndef CFG_OPUS_CHANNELS
#define CFG_OPUS_CHANNELS    1       /* 声道数: 1=mono 2=stereo（输入自动适配） */
#endif
#ifndef CFG_OPUS_BITRATE
#define CFG_OPUS_BITRATE     32000   /* 码率 bps；0 = 编码器自动选择 */
#endif
#ifndef CFG_OPUS_APP
#define CFG_OPUS_APP         OPUS_APPLICATION_AUDIO /* AUDIO/VOIP/RESTRICTED_LOWDELAY */
#endif
#ifndef CFG_PCM_BITS
#define CFG_PCM_BITS         16      /* PCM 位宽：内部 int16 缓冲，固定 16 */
#endif

/* ---- 派生常量 ---- */
#define OUT_RATE   CFG_OPUS_RATE                  /* 编解码工作采样率 */
#define FRAME_SZ   (OUT_RATE * CFG_OPUS_FRAME_MS / 1000)  /* 每帧样本数 */
#define OGG_RATE   48000                          /* Ogg granule 固定时间域 */
#define OGG_SCALE  (OGG_RATE / OUT_RATE)          /* 工作域 -> 48k 域 */
#define OUT_CH     CFG_OPUS_CHANNELS              /* 编解码声道数 */

/* ---- 参数合法性编译期检查 ---- */
#if CFG_OPUS_RATE != 8000 && CFG_OPUS_RATE != 12000 && CFG_OPUS_RATE != 16000 \
 && CFG_OPUS_RATE != 24000 && CFG_OPUS_RATE != 48000
#error "CFG_OPUS_RATE only supports 8000 / 12000 / 16000 / 24000 / 48000"
#endif
#if CFG_OPUS_FRAME_MS != 5 && CFG_OPUS_FRAME_MS != 10 && CFG_OPUS_FRAME_MS != 20 \
 && CFG_OPUS_FRAME_MS != 40 && CFG_OPUS_FRAME_MS != 60
#error "CFG_OPUS_FRAME_MS only supports 5 / 10 / 20 / 40 / 60"
#endif
#if CFG_OPUS_CHANNELS != 1 && CFG_OPUS_CHANNELS != 2
#error "CFG_OPUS_CHANNELS only supports 1 (mono) / 2 (stereo)"
#endif
#if CFG_PCM_BITS != 16
#error "PCM internal buffers are int16_t, CFG_PCM_BITS must be 16"
#endif
#if (OGG_RATE % OUT_RATE) != 0
#error "48000 % CFG_OPUS_RATE != 0 (internal macro inconsistency)"
#endif

/* ============================ 2. 测试入口（命令行参数） ============================
 * Opus 参数由上方宏指定；输入/输出文件由命令行参数指定：
 *   opuslite in.wav out.ogg    WAV -> Ogg Opus（方向按文件头自动判断）
 *   opuslite in.ogg out.wav    Ogg -> WAV
 *   opuslite --selftest x.ogg  内置正弦回环自测
 */
#ifndef SELFTEST_SECONDS
#define SELFTEST_SECONDS    3               /* 自测正弦时长（秒） */
#endif

#define MAX_PKT      1275         /* Opus 单包上限 */
#define MAX_DEC      5760         /* 解码单次最大样本/声道 (120 ms) */
#define OGG_BODY_MAX 65025        /* 单页 body 上限 */

/* ---------------------------------------------------------------- 小工具 */
static void die(const char *msg) { fprintf(stderr, "[E] %s\n", msg); exit(1); }
static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

/* 小端读写（纯字节拼装，不依赖 CPU 字节序） */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int64_t rd64s(const uint8_t *p)
{
    int64_t v = 0; int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, int64_t v)
{
    int i;
    for (i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xff);
}

/* ------------------------------------------------- Ogg CRC-32 (RFC 3533)
 * 多项式 0x04C11DB7，MSB 先行，初值 0，无反射                        */
static uint32_t crc_tab[256];
static void crc_init(void)
{
    uint32_t i, j, c;
    for (i = 0; i < 256; i++) {
        c = i << 24;
        for (j = 0; j < 8; j++)
            c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
        crc_tab[i] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        c = (c << 8) ^ crc_tab[(c >> 24) ^ d[i]];
    return c;
}

/* ---------------------------------------------------------- Ogg 封装器 */
typedef struct {
    FILE     *f;
    uint32_t  serial, seq;
    int       first_done;
    int       nseg;                    /* 当前页 lacing 段数 */
    uint8_t   seg[255];
    int       body_len;
    uint8_t  *body;
    int64_t   granule;                 /* 解码时间线累计样本 */
} OggMux;

static void om_init(OggMux *m, FILE *f, uint32_t serial)
{
    memset(m, 0, sizeof(*m));
    m->f = f;
    m->serial = serial;
    m->body = (uint8_t *)xmalloc(OGG_BODY_MAX);
}

static void om_flush(OggMux *m, int eos)
{
    uint8_t hdr[27 + 255];
    uint32_t c;

    if (m->nseg == 0) return;
    memcpy(hdr, "OggS", 4);
    hdr[4] = 0;
    hdr[5] = (uint8_t)((m->first_done ? 0 : 0x02) | (eos ? 0x04 : 0));
    /* RFC 3533 页头布局：granule@6(8B) serial@14(4B) seq@18(4B)
     *                    CRC@22(4B) 段数@26(1B) 段表@27..        */
    wr64(hdr + 6,  m->granule);
    wr32(hdr + 14, m->serial);
    wr32(hdr + 18, m->seq);
    wr32(hdr + 22, 0);                         /* CRC 先置 0 */
    hdr[26] = (uint8_t)m->nseg;
    memcpy(hdr + 27, m->seg, (size_t)m->nseg);

    c = crc_upd(0, hdr, 27 + (size_t)m->nseg);           /* 头+段表 */
    c = crc_upd(c, m->body, (size_t)m->body_len);        /* +body   */
    wr32(hdr + 22, c);

    fwrite(hdr, 1, 27 + (size_t)m->nseg, m->f);
    fwrite(m->body, 1, (size_t)m->body_len, m->f);

    m->seq++; m->first_done = 1;
    m->nseg = 0; m->body_len = 0;
}

/* 追加一个 Opus 包（按 255 规则生成 lacing 值，页满自动冲刷） */
static void om_packet(OggMux *m, const uint8_t *p, int n)
{
    int need = n / 255 + 1, i;
    if (m->nseg + need > 255 || m->body_len + n > OGG_BODY_MAX)
        om_flush(m, 0);
    for (i = 0; i < n / 255; i++) m->seg[m->nseg++] = 255;
    m->seg[m->nseg++] = (uint8_t)(n % 255);
    memcpy(m->body + m->body_len, p, (size_t)n);
    m->body_len += n;
}

/* ---------------------------------------------- Ogg Opus 头部（RFC 7845） */
static void write_ogg_headers(OggMux *m, int channels, int preskip)
{
    static const char vendor[] = "opus-1.3.1-lite-test";
    uint8_t head[19], tags[8 + 4 + 4 + sizeof(vendor)];
    uint8_t *q = tags;
    int vlen = (int)strlen(vendor);

    /* OpusHead */
    memset(head, 0, sizeof(head));
    memcpy(head, "OpusHead", 8);
    head[8] = 1;
    head[9] = (uint8_t)channels;
    wr16(head + 10, (uint16_t)preskip);
    wr32(head + 12, OUT_RATE);         /* 原始输入采样率（信息性） */
    om_packet(m, head, 19);
    om_flush(m, 0);                    /* BOS 页 */

    /* OpusTags */
    memcpy(q, "OpusTags", 8); q += 8;
    wr32(q, (uint32_t)vlen);  q += 4;
    memcpy(q, vendor, (size_t)vlen); q += vlen;
    wr32(q, 0);
    om_packet(m, tags, (int)(q - tags) + 4);
    om_flush(m, 0);
}

/* --------------------------------------------------------- 整文件读取 */
typedef struct { uint8_t *buf; size_t len; } FileBuf;

static FileBuf read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    FileBuf fb = { NULL, 0 };
    long sz;
    if (!f) die("cannot open input file");
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) die("empty input file");
    fb.buf = (uint8_t *)xmalloc((size_t)sz);
    if (fread(fb.buf, 1, (size_t)sz, f) != (size_t)sz) die("read failed");
    fb.len = (size_t)sz;
    fclose(f);
    return fb;
}

/* --------------------------------------------------------- WAV 解析 */
typedef struct {
    int channels, rate, bits, is_float;
    long data_off, data_size;
} WavInfo;

static void wav_parse(const uint8_t *b, size_t n, WavInfo *wi)
{
    size_t pos;
    memset(wi, 0, sizeof(*wi));
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4))
        die("not a RIFF/WAVE file");
    pos = 12;
    while (pos + 8 <= n) {
        uint32_t csz = rd32(b + pos + 4);
        if (!memcmp(b + pos, "fmt ", 4)) {
            uint16_t fmt = rd16(b + pos + 8);
            if (fmt == 0xFFFE && pos + 8 + 40 <= n)   /* EXTENSIBLE */
                fmt = rd16(b + pos + 8 + 24);
            wi->channels = rd16(b + pos + 10);
            wi->rate     = (int)rd32(b + pos + 12);
            wi->bits     = rd16(b + pos + 22);
            wi->is_float = (fmt == 3);
            if (fmt != 1 && fmt != 3) die("unsupported WAV format (need PCM)");
        } else if (!memcmp(b + pos, "data", 4)) {
            wi->data_off  = (long)(pos + 8);
            wi->data_size = (long)csz;
            break;
        }
        pos += 8 + csz + (csz & 1);
    }
    if (!wi->channels || !wi->data_size) die("WAV fmt/data chunk missing");
    if (wi->channels < 1 || wi->channels > 2) die("only mono/stereo supported");
    if (wi->bits == 0 || (wi->bits & 7) || wi->bits > 32)
        die("unsupported bit depth");
}

/* 任意位深 -> int16 交织缓冲；返回帧数 */
static long wav_to_i16(const uint8_t *b, const WavInfo *wi, int16_t **out)
{
    long frames = wi->data_size / (wi->bits / 8) / wi->channels;
    int16_t *pcm = (int16_t *)xmalloc((size_t)frames * (size_t)wi->channels * 2);
    long i;
    int c;
    const uint8_t *d = b + wi->data_off;

    for (i = 0; i < frames; i++)
        for (c = 0; c < wi->channels; c++) {
            size_t k = (size_t)(i * wi->channels + c);
            int32_t v = 0;
            switch (wi->bits) {
            case 8:  v = ((int)d[k] - 128) << 8; break;
            case 16: v = (int16_t)rd16(d + k * 2); break;
            case 24: {
                uint32_t u = (uint32_t)d[k*3] | ((uint32_t)d[k*3+1] << 8) |
                             ((uint32_t)d[k*3+2] << 16);
                v = ((int32_t)(u << 8)) >> 16;   /* 取高 16 位，保号 */
                break;
            }
            case 32:
                if (wi->is_float) {
                    uint32_t u = rd32(d + k * 4);
                    float fv; memcpy(&fv, &u, 4);
                    fv *= 32768.f;
                    v = fv > 32767.f ? 32767 :
                        (fv < -32768.f ? -32768 : (int32_t)fv);
                } else {
                    v = (int32_t)((int64_t)(int32_t)rd32(d + k * 4) / 65536);
                }
                break;
            default: die("unsupported bit depth");
            }
            pcm[i * wi->channels + c] = (int16_t)v;
        }
    *out = pcm;
    return frames;
}

/* --------------------------------------------- 线性重采样 -> OUT_RATE */
static long resample_pcm(const int16_t *in, long frames, int ch,
                         int in_rate, int16_t **out)
{
    if (in_rate == OUT_RATE) {
        *out = (int16_t *)xmalloc((size_t)frames * (size_t)ch * 2);
        memcpy(*out, in, (size_t)frames * (size_t)ch * 2);
        return frames;
    } else {
        double step = (double)in_rate / OUT_RATE;
        long n_out = (long)((double)frames * OUT_RATE / in_rate + 0.5);
        int16_t *r = (int16_t *)xmalloc((size_t)n_out * (size_t)ch * 2);
        long k;
        int c;
        for (k = 0; k < n_out; k++) {
            double pos = k * step;
            long i0 = (long)pos;
            double f = pos - i0;
            long i1 = (i0 + 1 < frames) ? i0 + 1 : frames - 1;
            for (c = 0; c < ch; c++) {
                double v = in[i0 * ch + c] * (1.0 - f) + in[i1 * ch + c] * f;
                r[k * ch + c] = (int16_t)(v > 32767 ? 32767 :
                                          (v < -32768 ? -32768 : v));
            }
        }
        *out = r;
        return n_out;
    }
}

/* -------------------------------------- 声道数适配（折叠 / 扩展到 OUT_CH）
 * 2->1: (L+R)/2 就地折叠；1->2: 复制扩展（分配新缓冲替换原缓冲） */
static void match_channels(int16_t **ppcm, long frames, int in_ch)
{
    int16_t *pcm = *ppcm;
    long i;
    if (in_ch == OUT_CH) return;
    if (in_ch == 2 && OUT_CH == 1) {
        for (i = 0; i < frames; i++) {
            int s = (int)pcm[i * 2] + (int)pcm[i * 2 + 1];  /* int 累加防溢出 */
            pcm[i] = (int16_t)(s / 2);
        }
        return;
    }
    if (in_ch == 1 && OUT_CH == 2) {
        int16_t *s = (int16_t *)xmalloc((size_t)frames * 2 * 2);
        for (i = 0; i < frames; i++) { s[i * 2] = pcm[i]; s[i * 2 + 1] = pcm[i]; }
        free(pcm);
        *ppcm = s;
        return;
    }
    die("unsupported channel conversion");
}

/* ============================================================= WAV -> OGG */
static void opus_wav_to_ogg(const char *in_path, const char *out_path)
{
    FileBuf fb = read_whole_file(in_path);
    WavInfo wi;
    int16_t *pcm_raw = NULL, *pcm;
    long raw_frames, frames, real_frames;
    OpusEncoder *enc;
    int err, i, nframes;
    opus_int32 preskip;                 /* ctl 指针参数必须用库类型，
                                           arm-none-eabi 上 int!=opus_int32 */
    OggMux om;
    FILE *fo;
    uint8_t pkt[MAX_PKT];

    wav_parse(fb.buf, fb.len, &wi);
    printf("[wav ] %d Hz, %d ch, %d-bit %s, %ld frames\n",
           wi.rate, wi.channels, wi.bits, wi.is_float ? "float" : "int",
           wi.data_size / (wi.bits / 8) / wi.channels);

    raw_frames = wav_to_i16(fb.buf, &wi, &pcm_raw);
    if (wi.channels != OUT_CH)
        printf("[mix ] %d ch -> %d ch\n", wi.channels, OUT_CH);
    match_channels(&pcm_raw, raw_frames, wi.channels);
    frames = resample_pcm(pcm_raw, raw_frames, OUT_CH, wi.rate, &pcm);
    if (wi.rate != OUT_RATE)
        printf("[rs  ] %d Hz -> %d Hz: %ld -> %ld frames\n",
               wi.rate, OUT_RATE, raw_frames, frames);

    enc = opus_encoder_create(OUT_RATE, OUT_CH, CFG_OPUS_APP, &err);
    if (err != OPUS_OK) die("opus_encoder_create failed");
    if (CFG_OPUS_BITRATE > 0)
        opus_encoder_ctl(enc, OPUS_SET_BITRATE(CFG_OPUS_BITRATE));
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&preskip));   /* OUT_RATE 域样本 */

    fo = fopen(out_path, "wb");
    if (!fo) die("cannot open output file");
    om_init(&om, fo, 0x1234ABCDu);
    write_ogg_headers(&om, OUT_CH, preskip * OGG_SCALE);  /* RFC7845: 48k 域 */

    /* 尾部补 preskip+帧对齐静音，保证解码时长 == 原始时长 */
    real_frames = frames;
    nframes = (int)((frames + preskip + FRAME_SZ - 1) / FRAME_SZ);
    {
        int16_t *np = (int16_t *)realloc(
            pcm, (size_t)nframes * FRAME_SZ * (size_t)OUT_CH * 2);
        if (!np) die("realloc failed");
        pcm = np;
    }
    memset(pcm + frames * OUT_CH, 0,
           (size_t)(nframes * FRAME_SZ - frames) * (size_t)OUT_CH * 2);

    for (i = 0; i < nframes; i++) {
        int nb = opus_encode(enc, pcm + (size_t)i * FRAME_SZ * OUT_CH,
                             FRAME_SZ, pkt, MAX_PKT);
        if (nb < 0) die("opus_encode failed");
        om_packet(&om, pkt, nb);
        om.granule += (int64_t)FRAME_SZ * OGG_SCALE;   /* 48k 域递增 */
    }
    om.granule = (int64_t)(preskip + real_frames) * OGG_SCALE; /* 末页精确 */
    om_flush(&om, 1);                      /* EOS 页 */

    printf("[enc ] %d x %dms frames @ %d Hz %d ch, preskip=%d (%d Hz) / %d (48k)\n",
           nframes, CFG_OPUS_FRAME_MS, OUT_RATE, OUT_CH,
           preskip, OUT_RATE, preskip * OGG_SCALE);
    printf("[ogg ] %s written, avg %.1f kb/s\n", out_path,
           (double)ftell(fo) * 8.0 / ((double)real_frames / OUT_RATE) / 1000.0);

    fclose(fo);
    opus_encoder_destroy(enc);
    free(pcm_raw); free(pcm); free(om.body); free(fb.buf);
}

/* ============================================================= OGG -> WAV */
static void opus_ogg_to_wav(const char *in_path, const char *out_path)
{
    FileBuf fb = read_whole_file(in_path);
    const uint8_t *b = fb.buf;
    size_t n = fb.len, pos = 0;
    int channels = 0, preskip48 = 0, preskip = 0, have_head = 0, saw_tags = 0;
    int64_t last_granule = -1, decoded_total = 0;
    OpusDecoder *dec = NULL;
    int err;
    long target = -1, out_frames = 0, out_cap = 0;
    int16_t *out = NULL;
    int16_t tmp[MAX_DEC * 2];
    size_t pend_cap = 1 << 16;
    uint8_t *pend = (uint8_t *)xmalloc(pend_cap);
    size_t pend_len = 0;

    while (pos + 27 <= n && !memcmp(b + pos, "OggS", 4)) {
        int flags = (int)b[pos + 5];
        int nseg  = (int)b[pos + 26];
        const uint8_t *seg  = b + pos + 27;
        size_t body_len = 0, off = 0;
        int64_t g;
        int i;

        if (b[pos + 4] != 0) die("bad ogg version");
        for (i = 0; i < nseg; i++) body_len += seg[i];
        if (pos + 27 + (size_t)nseg + body_len > n) break;

        g = rd64s(b + pos + 6);
        if (g > 0) last_granule = g;

        {
            const uint8_t *body = b + pos + 27 + (size_t)nseg;
            if (!(flags & 0x01)) pend_len = 0;     /* 非续页清挂起包 */

            for (i = 0; i < nseg; i++) {
                size_t l = seg[i];
                if (pend_len + l > pend_cap) {
                    while (pend_len + l > pend_cap) pend_cap *= 2;
                    pend = (uint8_t *)realloc(pend, pend_cap);
                    if (!pend) die("realloc failed");
                }
                memcpy(pend + pend_len, body + off, l);
                pend_len += l; off += l;

                if (seg[i] == 255) continue;       /* 包未结束，跨页继续 */

                /* ---- 完整包到达 ---- */
                if (!have_head) {
                    if (pend_len < 19 || memcmp(pend, "OpusHead", 8))
                        die("missing OpusHead");
                    channels  = pend[9];
                    preskip48 = rd16(pend + 10);          /* RFC7845: 48k 域 */
                    preskip   = preskip48 * OUT_RATE / OGG_RATE;  /* 解码域 */
                    if (channels < 1 || channels > 2) die("bad channel count");
                    dec = opus_decoder_create(OUT_RATE, channels, &err);
                    if (err != OPUS_OK) die("opus_decoder_create failed");
                    have_head = 1;
                } else if (!saw_tags && pend_len >= 8 &&
                           !memcmp(pend, "OpusTags", 8)) {
                    saw_tags = 1;                  /* 跳过标签包 */
                } else {
                    int ns = opus_decode(dec, pend, (int)pend_len,
                                         tmp, MAX_DEC, 0);
                    if (ns < 0) die("opus_decode failed");
                    if (ns > 0) {
                        int64_t s0 = decoded_total;         /* 本包首样本 */
                        int64_t s1 = decoded_total + ns;
                        decoded_total = s1;
                        if (s0 < preskip) s0 = preskip;     /* 去头 */
                        if (target < 0 && last_granule > preskip48 && saw_tags)
                            target = (long)(last_granule - preskip48)
                                     * OUT_RATE / OGG_RATE;
                        if (target >= 0 && s1 > preskip + target)
                            s1 = preskip + target;          /* 截尾 */
                        {
                            long cnt = (long)(s1 - s0);
                            if (cnt > 0) {
                                if (out_frames + cnt > out_cap) {
                                    out_cap = (out_frames + cnt) * 2 + OUT_RATE;
                                    out = (int16_t *)realloc(out,
                                        (size_t)out_cap * (size_t)channels * 2);
                                    if (!out) die("realloc failed");
                                }
                                memcpy(out + (size_t)out_frames * channels,
                                       tmp + (size_t)(s0 - (decoded_total - ns)) * channels,
                                       (size_t)cnt * (size_t)channels * 2);
                                out_frames += cnt;
                            }
                        }
                    }
                }
                pend_len = 0;
            }
        }
        pos += 27 + (size_t)nseg + body_len;
    }
    if (!have_head) die("no OpusHead found");
    if (dec) opus_decoder_destroy(dec);

    /* ---- 声道适配到 OUT_CH，输出 OUT_RATE / OUT_CH / 16-bit s16le ---- */
    if (out && out_frames > 0 && channels != OUT_CH)
        printf("[mix ] %d ch -> %d ch\n", channels, OUT_CH);
    if (out && out_frames > 0)
        match_channels(&out, out_frames, channels);
    {
        FILE *fo = fopen(out_path, "wb");
        uint8_t hdr[44];
        uint32_t dsz = (uint32_t)(out_frames * OUT_CH * 2);
        if (!fo) die("cannot open output wav");
        memcpy(hdr, "RIFF", 4); wr32(hdr + 4, 36 + dsz); memcpy(hdr + 8, "WAVE", 4);
        memcpy(hdr + 12, "fmt ", 4); wr32(hdr + 16, 16);
        wr16(hdr + 20, 1); wr16(hdr + 22, (uint16_t)OUT_CH);
        wr32(hdr + 24, OUT_RATE); wr32(hdr + 28, OUT_RATE * (uint32_t)OUT_CH * 2);
        wr16(hdr + 32, (uint16_t)(OUT_CH * 2)); wr16(hdr + 34, 16);
        memcpy(hdr + 36, "data", 4); wr32(hdr + 40, dsz);
        fwrite(hdr, 1, 44, fo);
        if (out && out_frames > 0)
            fwrite(out, 2, (size_t)out_frames * (size_t)OUT_CH, fo);
        fclose(fo);
    }
    printf("[dec ] %d ch -> %d ch @ %d Hz, preskip=%d(48k)/%d(%d Hz), "
           "last_granule=%lld, %ld frames -> %s\n",
           channels, OUT_CH, OUT_RATE, preskip48, preskip, OUT_RATE,
           (long long)last_granule, out_frames, out_path);

    free(pend); free(out); free(fb.buf);
}

/* ============================================================= 自测回环 */
static void selftest(const char *ogg_path)
{
    long i, n = OUT_RATE * (long)SELFTEST_SECONDS;
    int16_t *pcm = (int16_t *)xmalloc((size_t)n * (size_t)OUT_CH * 2);
    FILE *f;
    uint8_t wh[44];
    double energy = 0.0, err = 0.0, best_corr = -1e30;
    int lag, best_lag = 0;

    /* 1) 生成正弦（声道间同相）-> WAV */
    for (i = 0; i < n; i++) {
        int16_t v = (int16_t)(12000.0 *
            sin(2.0 * 3.14159265358979 * 440.0 * (double)i / OUT_RATE));
        for (lag = 0; lag < OUT_CH; lag++)       /* lag 此处复用为声道下标 */
            pcm[i * OUT_CH + lag] = v;
    }
    f = fopen("selftest_in.wav", "wb");
    if (!f) die("selftest wav open failed");
    memcpy(wh, "RIFF", 4); wr32(wh + 4, 36 + (uint32_t)(n * OUT_CH * 2));
    memcpy(wh + 8, "WAVE", 4); memcpy(wh + 12, "fmt ", 4); wr32(wh + 16, 16);
    wr16(wh + 20, 1); wr16(wh + 22, (uint16_t)OUT_CH); wr32(wh + 24, OUT_RATE);
    wr32(wh + 28, OUT_RATE * (uint32_t)OUT_CH * 2);
    wr16(wh + 32, (uint16_t)(OUT_CH * 2)); wr16(wh + 34, 16);
    memcpy(wh + 36, "data", 4); wr32(wh + 40, (uint32_t)(n * OUT_CH * 2));
    fwrite(wh, 1, 44, f);
    fwrite(pcm, 2, (size_t)n * (size_t)OUT_CH, f); fclose(f);

    /* 2) WAV -> OGG -> WAV 完整链路 */
    opus_wav_to_ogg("selftest_in.wav", ogg_path);
    opus_ogg_to_wav(ogg_path, "selftest_out.wav");

    /* 3) 读回输出，延迟搜索对齐后算 SNR（声道 0） */
    {
        FileBuf fb = read_whole_file("selftest_out.wav");
        WavInfo wi;
        int16_t *out_pcm = NULL;
        long out_n;

        wav_parse(fb.buf, fb.len, &wi);          /* 必须先解析头部 */
        out_n = wav_to_i16(fb.buf, &wi, &out_pcm);
        if (out_n < n / 2) die("round-trip output too short");
        {
            long m = (n < out_n) ? n : out_n;
            for (lag = 0; lag < 1500; lag++) {
                double corr = 0.0;
                for (i = 9600; i + lag < m && i < 96000; i++)
                    corr += (double)pcm[i * OUT_CH] * out_pcm[(i + lag) * OUT_CH];
                if (corr > best_corr) { best_corr = corr; best_lag = lag; }
            }
            lag = best_lag;
            for (i = 9600; i + lag < m && i < 96000; i++) {
                double d = (double)pcm[i * OUT_CH] - out_pcm[(i + lag) * OUT_CH];
                err    += d * d;
                energy += (double)pcm[i * OUT_CH] * pcm[i * OUT_CH];
            }
        }
        {
            double snr = 10.0 * log10(energy / (err > 0 ? err : 1e-9));
            printf("[test] %d Hz %d ch %d ms, delay=%d samples (%.1f ms), "
                   "SNR=%.1f dB -> %s\n",
                   OUT_RATE, OUT_CH, CFG_OPUS_FRAME_MS,
                   best_lag, best_lag * 1000.0 / OUT_RATE, snr,
                   snr > 20.0 ? "PASS" : "FAIL");
        }
        free(out_pcm); free(fb.buf);
    }
    free(pcm);
}

/* ============================================================= 入口
 * Opus 参数由宏指定；文件路径由命令行参数指定 */
int main(int argc, char **argv)
{
    crc_init();
    if (argc == 3 && !strcmp(argv[1], "--selftest")) {
        selftest(argv[2]);
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr,
            "usage: %s input.wav output.ogg\n"
            "       %s input.ogg output.wav\n"
            "       %s --selftest out.ogg\n", argv[0], argv[0], argv[0]);
        return 1;
    }
    {
        FileBuf probe = read_whole_file(argv[1]);
        int is_wav = probe.len >= 12 && !memcmp(probe.buf, "RIFF", 4);
        int is_ogg = probe.len >= 4  && !memcmp(probe.buf, "OggS", 4);
        free(probe.buf);
        if (is_wav)      opus_wav_to_ogg(argv[1], argv[2]);
        else if (is_ogg) opus_ogg_to_wav(argv[1], argv[2]);
        else die("input is neither WAV nor Ogg");
    }
    return 0;
}
