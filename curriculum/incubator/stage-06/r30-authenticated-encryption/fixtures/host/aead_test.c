#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aead.h"
void rk_test_reset(void);void rk_test_fail(int);void rk_test_async(int,int);unsigned int rk_test_live(void);unsigned int rk_test_clears(void);
static unsigned int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"check %u failed: %s:%d: %s\n",checks,__FILE__,__LINE__,#x);exit(1);}}while(0)
static void unchanged(const u8 *p,size_t n,u8 v){for(size_t i=0;i<n;i++)CHECK(p[i]==v);}
int main(void)
{
	u8 key[32],nonce[12],aad[32],plain[80],sealed[96],opened[80],overlap[128];size_t sl,pl;int e;
	static const int async_modes[] = { -EINPROGRESS, -EBUSY };
	for(size_t i=0;i<sizeof(key);i++) key[i]=(u8)i;
	for(size_t i=0;i<sizeof(nonce);i++) nonce[i]=(u8)(0xa0+i);
	for(size_t i=0;i<sizeof(aad);i++) aad[i]=(u8)(0x40+i);
	for(size_t i=0;i<sizeof(plain);i++) plain[i]=(u8)(3*i);
	for(size_t al=0;al<=32;al++)for(size_t n=0;n<=64;n++){
		rk_test_reset();memset(sealed,0xcc,sizeof(sealed));sl=999;e=rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,al,plain,n,sealed,sizeof(sealed),&sl);CHECK(e==0);CHECK(sl==n+16);CHECK(rk_test_live()==0);CHECK(rk_test_clears()==2);
		memset(opened,0xdd,sizeof(opened));pl=888;e=rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,al,sealed,sl,opened,sizeof(opened),&pl);CHECK(e==0);CHECK(pl==n);CHECK(!memcmp(opened,plain,n));CHECK(rk_test_live()==0);
		if(sl){sealed[sl-1]^=1;memset(opened,0x5a,sizeof(opened));pl=77;e=rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,al,sealed,sl,opened,sizeof(opened),&pl);CHECK(e==-EBADMSG);CHECK(pl==77);unchanged(opened,sizeof(opened),0x5a);}
	}
	for(size_t mode=0;mode<sizeof(async_modes)/sizeof(async_modes[0]);mode++){
		int first=async_modes[mode];
		rk_test_reset();rk_test_async(first,0);sl=0;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,4,plain,8,sealed,sizeof(sealed),&sl)==0);CHECK(sl==24);
		rk_test_reset();rk_test_async(first,0);pl=0;CHECK(rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,4,sealed,sl,opened,sizeof(opened),&pl)==0);CHECK(pl==8);CHECK(!memcmp(opened,plain,8));
		rk_test_reset();rk_test_async(first,-EIO);memset(sealed,0xa5,sizeof(sealed));sl=41;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,4,plain,8,sealed,sizeof(sealed),&sl)==-EIO);CHECK(sl==41);unchanged(sealed,sizeof(sealed),0xa5);
	}
	for(int f=1;f<=7;f++){rk_test_reset();rk_test_fail(f);memset(sealed,0xa5,sizeof(sealed));sl=19;e=rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,2,plain,5,sealed,sizeof(sealed),&sl);CHECK(e<0);CHECK(sl==19);unchanged(sealed,sizeof(sealed),0xa5);CHECK(rk_test_live()==0);}
	rk_test_reset();memcpy(overlap,plain,64);sl=0;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,8,overlap,64,overlap+3,80,&sl)==0);pl=0;CHECK(rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,8,overlap+3,sl,overlap,64,&pl)==0);CHECK(pl==64);CHECK(!memcmp(overlap,plain,64));
	rk_test_reset();sl=0;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,8,plain,16,sealed,sizeof(sealed),&sl)==0);
	for(int kind=0;kind<4;kind++){
		u8 badkey[32],badnonce[12],badaad[32],badsealed[96];
		memcpy(badkey,key,sizeof(key));memcpy(badnonce,nonce,sizeof(nonce));memcpy(badaad,aad,sizeof(aad));memcpy(badsealed,sealed,sl);
		if(kind==0)badsealed[0]^=1;else if(kind==1)badaad[0]^=1;else if(kind==2)badnonce[0]^=1;else badkey[0]^=1;
		memset(opened,0x6b,sizeof(opened));pl=23;e=rk_aead_open_with(&rk_kernel_aead_ops,badkey,badnonce,badaad,8,badsealed,sl,opened,sizeof(opened),&pl);CHECK(e==-EBADMSG);CHECK(pl==23);unchanged(opened,sizeof(opened),0x6b);
	}
	rk_test_reset();rk_test_async(-EIO,0);memset(sealed,0x71,sizeof(sealed));sl=31;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,2,plain,4,sealed,sizeof(sealed),&sl)==-EIO);CHECK(sl==31);unchanged(sealed,sizeof(sealed),0x71);
	rk_test_reset();rk_test_async(1,0);memset(sealed,0x72,sizeof(sealed));sl=32;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,2,plain,4,sealed,sizeof(sealed),&sl)==-EPROTO);CHECK(sl==32);unchanged(sealed,sizeof(sealed),0x72);
	memset(sealed,0xa5,sizeof(sealed));sl=12;CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,33,plain,1,sealed,sizeof(sealed),&sl)==-EMSGSIZE);CHECK(sl==12);unchanged(sealed,sizeof(sealed),0xa5);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,0,sealed,15,opened,sizeof(opened),&pl)==-EMSGSIZE);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,0,sealed,81,opened,sizeof(opened),&pl)==-EMSGSIZE);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,0,plain,65,sealed,sizeof(sealed),&sl)==-EMSGSIZE);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,0,plain,2,sealed,17,&sl)==-ENOSPC);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops,key,nonce,aad,0,sealed,18,opened,1,&pl)==-ENOSPC);
	CHECK(rk_aead_seal_with(NULL,key,nonce,aad,0,plain,1,sealed,sizeof(sealed),&sl)==-EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,NULL,nonce,aad,0,plain,1,sealed,sizeof(sealed),&sl)==-EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,NULL,aad,0,plain,1,sealed,sizeof(sealed),&sl)==-EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,NULL,0,plain,1,sealed,sizeof(sealed),&sl)==-EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,0,NULL,0,sealed,sizeof(sealed),&sl)==-EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,0,plain,1,NULL,sizeof(sealed),&sl)==-EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops,key,nonce,aad,0,plain,1,sealed,sizeof(sealed),NULL)==-EINVAL);
	printf("[+] %u authenticated-encryption assertions passed\n",checks);return 0;
}
