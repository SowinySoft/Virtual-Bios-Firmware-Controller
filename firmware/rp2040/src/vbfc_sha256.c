#include "vbfc_sha256.h"
#include "vbfc_pubkey.h"
#include <string.h>

/* ─── FIPS 180-4 SHA-256 (pure software) ──────────────────────────────── */

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^((~(x))&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define S0(x) (ROTR(x, 2)^ROTR(x,13)^ROTR(x,22))
#define S1(x) (ROTR(x, 6)^ROTR(x,11)^ROTR(x,25))
#define s0(x) (ROTR(x, 7)^ROTR(x,18)^((x)>>3))
#define s1(x) (ROTR(x,17)^ROTR(x,19)^((x)>>10))

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint32_t count;       /* total bytes processed */
    int      buf_used;    /* bytes in buf */
} sha256_ctx;

static void sha256_transform(sha256_ctx *ctx) {
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    for (int i=0;i<16;i++) {
        w[i] = ((uint32_t)ctx->buf[i*4]<<24)|((uint32_t)ctx->buf[i*4+1]<<16)|
               ((uint32_t)ctx->buf[i*4+2]<<8)|ctx->buf[i*4+3];
    }
    for (int i=16;i<64;i++) w[i] = s1(w[i-2])+w[i-7]+s0(w[i-15])+w[i-16];
    a=ctx->h[0]; b=ctx->h[1]; c=ctx->h[2]; d=ctx->h[3];
    e=ctx->h[4]; f=ctx->h[5]; g=ctx->h[6]; h=ctx->h[7];
    for (int i=0;i<64;i++) {
        t1 = h + S1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = S0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->h[0]+=a; ctx->h[1]+=b; ctx->h[2]+=c; ctx->h[3]+=d;
    ctx->h[4]+=e; ctx->h[5]+=f; ctx->h[6]+=g; ctx->h[7]+=h;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    ctx->count += len;
    if (ctx->buf_used) {
        int fill = 64 - ctx->buf_used;
        if ((int)len < fill) { memcpy(ctx->buf+ctx->buf_used,data,len); ctx->buf_used+=(int)len; return; }
        memcpy(ctx->buf+ctx->buf_used,data,fill); sha256_transform(ctx); ctx->buf_used=0;
        data+=fill; len-=fill;
    }
    while (len>=64) {
        memcpy(ctx->buf,data,64); sha256_transform(ctx); data+=64; len-=64;
    }
    if (len) { memcpy(ctx->buf,data,len); ctx->buf_used=(int)len; }
}

static void sha256_final(sha256_ctx *ctx, uint8_t out[32]) {
    uint64_t bits = ctx->count * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    while (ctx->buf_used != 56) { pad=0; sha256_update(ctx, &pad, 1); }
    uint8_t bits_be[8];
    for (int i=0;i<8;i++) bits_be[7-i] = (uint8_t)(bits>>(i*8));
    sha256_update(ctx, bits_be, 8);
    for (int i=0;i<8;i++) {
        out[i*4]=ctx->h[i]>>24; out[i*4+1]=ctx->h[i]>>16; out[i*4+2]=ctx->h[i]>>8; out[i*4+3]=ctx->h[i];
    }
}

/* ─── public API ───────────────────────────────────────────────────────── */

void vbfc_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    sha256_ctx ctx;
    memset(&ctx,0,sizeof(ctx));
    ctx.h[0]=0x6a09e667; ctx.h[1]=0xbb67ae85; ctx.h[2]=0x3c6ef372; ctx.h[3]=0xa54ff53a;
    ctx.h[4]=0x510e527f; ctx.h[5]=0x9b05688c; ctx.h[6]=0x1f83d9ab; ctx.h[7]=0x5be0cd19;
    sha256_update(&ctx,data,len);
    sha256_final(&ctx,out);
}

static sha256_ctx g_stream;

void vbfc_sha256_stream_start(vbfc_sha256_stream_t *s) {
    s->_sdk=(void*)1;
    memset(&g_stream,0,sizeof(g_stream));
    g_stream.h[0]=0x6a09e667; g_stream.h[1]=0xbb67ae85; g_stream.h[2]=0x3c6ef372; g_stream.h[3]=0xa54ff53a;
    g_stream.h[4]=0x510e527f; g_stream.h[5]=0x9b05688c; g_stream.h[6]=0x1f83d9ab; g_stream.h[7]=0x5be0cd19;
}

void vbfc_sha256_stream_update(vbfc_sha256_stream_t *s, const uint8_t *data, size_t len) {
    (void)s; sha256_update(&g_stream,data,len);
}

void vbfc_sha256_stream_finish(vbfc_sha256_stream_t *s, uint8_t out[32]) {
    sha256_final(&g_stream,out); s->_sdk=NULL;
}

/* ─── HMAC-SHA256 (RFC 2104) ──────────────────────────────────────────── */

#define HMAC_B 64u

void vbfc_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len,
                      uint8_t out[32]) {
    uint8_t k0[HMAC_B]; memset(k0,0,HMAC_B);
    if (key_len>HMAC_B) vbfc_sha256(key,key_len,k0);
    else memcpy(k0,key,key_len);
    uint8_t k_ipad[HMAC_B], k_opad[HMAC_B];
    for (uint32_t i=0;i<HMAC_B;i++) { k_ipad[i]=k0[i]^0x36; k_opad[i]=k0[i]^0x5c; }
    uint8_t inner[32];
    sha256_ctx ctx; memset(&ctx,0,sizeof(ctx));
    ctx.h[0]=0x6a09e667; ctx.h[1]=0xbb67ae85; ctx.h[2]=0x3c6ef372; ctx.h[3]=0xa54ff53a;
    ctx.h[4]=0x510e527f; ctx.h[5]=0x9b05688c; ctx.h[6]=0x1f83d9ab; ctx.h[7]=0x5be0cd19;
    sha256_update(&ctx,k_ipad,HMAC_B);
    if (msg_len) sha256_update(&ctx,msg,msg_len);
    sha256_final(&ctx,inner);
    memset(&ctx,0,sizeof(ctx));
    ctx.h[0]=0x6a09e667; ctx.h[1]=0xbb67ae85; ctx.h[2]=0x3c6ef372; ctx.h[3]=0xa54ff53a;
    ctx.h[4]=0x510e527f; ctx.h[5]=0x9b05688c; ctx.h[6]=0x1f83d9ab; ctx.h[7]=0x5be0cd19;
    sha256_update(&ctx,k_opad,HMAC_B);
    sha256_update(&ctx,inner,32);
    sha256_final(&ctx,out);
}

/* ─── compiled-in device key ───────────────────────────────────────────── */

static const uint8_t g_device_key[] = VBFC_HMAC_KEY;
const uint8_t *vbfc_device_hmac_key(void) { return g_device_key; }
size_t vbfc_device_hmac_key_len(void) { return sizeof(g_device_key); }