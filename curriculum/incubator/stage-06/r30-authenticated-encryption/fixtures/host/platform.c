#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "aead.h"

struct fake_tfm { u8 key[32]; };
struct fake_req { struct fake_tfm *tfm; u8 *buf; unsigned int total, ad, crypt; u8 *iv; };
static struct fake_req *active;
static int fail_at, submit_error, completion_error;
static unsigned int allocations, frees, request_allocations, request_frees, clears;

void rk_test_reset(void) { fail_at=0;submit_error=0;completion_error=0;allocations=frees=request_allocations=request_frees=clears=0; }
void rk_test_fail(int n) { fail_at=n; }
void rk_test_async(int first,int final) { submit_error=first;completion_error=final; }
unsigned int rk_test_live(void) { return allocations-frees+request_allocations-request_frees; }
unsigned int rk_test_clears(void) { return clears; }

static void *f_alloc(void) { struct fake_tfm *p; if(fail_at==1)return (void *)(intptr_t)-ENOMEM; if(fail_at==2)return NULL; p=calloc(1,sizeof(*p)); if(p)allocations++; return p; }
static bool f_is_error(const void *p) { return (intptr_t)p<0 && (intptr_t)p>=-4095; }
static int f_pointer_error(const void *p) { return (int)(intptr_t)p; }
static unsigned int f_iv_size(void *p) { (void)p; return fail_at==3?8:12; }
static unsigned int f_auth_size(void *p) { (void)p; return fail_at==6?12:16; }
static int f_set_key(void *p,const u8 *k,unsigned int n) { if(fail_at==4)return -EKEYREJECTED; if(n!=32)return -EINVAL; memcpy(((struct fake_tfm *)p)->key,k,n); return 0; }
static int f_set_auth(void *p,unsigned int n) { (void)p;(void)n;return fail_at==5?-EINVAL:0; }
static void *f_req_alloc(void *p) { struct fake_req *r;if(fail_at==7)return NULL;r=calloc(1,sizeof(*r));if(r){r->tfm=p;request_allocations++;active=r;}return r; }
static void f_req_free(void *p) { free(p);request_frees++;active=NULL; }
static void f_tfm_free(void *p) { memset(p,0,sizeof(struct fake_tfm));free(p);frees++; }
static void f_sg(void *sg,void *buf,unsigned int n) { (void)sg;active->buf=buf;active->total=n; }
static void f_wait_init(void *w) { memset(w,0,sizeof(unsigned long)*8); }
static void f_callback(void *r,unsigned int flags,void *w) { (void)r;(void)w;if(flags!=3)abort(); }
static void f_ad(void *r,unsigned int n) { ((struct fake_req *)r)->ad=n; }
static void f_crypt(void *r,void *sg,unsigned int n,u8 *iv) { (void)sg;((struct fake_req *)r)->crypt=n;((struct fake_req *)r)->iv=iv; }
static void tag(struct fake_req *r,u8 out[16],unsigned int plain)
{
	for(unsigned int i=0;i<16;i++)out[i]=(u8)(r->tfm->key[i]^r->iv[i%12]^i);
	for(unsigned int i=0;i<r->ad;i++)out[i%16]^=(u8)(r->buf[i]+i);
	for(unsigned int i=0;i<plain;i++)out[i%16]^=(u8)(r->buf[r->ad+i]+3*i);
}
static int f_encrypt(void *p) { struct fake_req *r=p;u8 t[16];for(unsigned int i=0;i<r->crypt;i++)r->buf[r->ad+i]^=(u8)(r->tfm->key[i%32]^r->iv[i%12]);tag(r,t,r->crypt);memcpy(r->buf+r->ad+r->crypt,t,16);return submit_error; }
static int f_decrypt(void *p) { struct fake_req *r=p;u8 got[16],want[16];unsigned int n=r->crypt-16;memcpy(got,r->buf+r->ad+n,16);tag(r,want,n);for(unsigned int i=0;i<n;i++)r->buf[r->ad+i]^=(u8)(r->tfm->key[i%32]^r->iv[i%12]);if(memcmp(got,want,16))return -EBADMSG;return submit_error; }
static int f_wait(int e,void *w) { (void)w;if(e==-EINPROGRESS||e==-EBUSY)return completion_error;return e; }
static void f_clear(void *p,size_t n) { memset(p,0,n);clears++; }
const struct rk_aead_ops rk_kernel_aead_ops={f_alloc,f_is_error,f_pointer_error,f_iv_size,f_auth_size,f_set_key,f_set_auth,f_req_alloc,f_req_free,f_tfm_free,f_sg,f_wait_init,f_callback,f_ad,f_crypt,f_encrypt,f_decrypt,f_wait,f_clear};
