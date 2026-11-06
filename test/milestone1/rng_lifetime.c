/* Desktop ownership instrumentation for real pinned Security2 calls.
 * Tracks init/free pairs because host mbedTLS builds can omit the target's
 * dynamically allocated threading locks. No radio or target heap simulation.
 */
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int entropy_live, drbg_live, entropy_init_count, entropy_free_count;
static int drbg_init_count, drbg_free_count, failure_mode;
void __real_mbedtls_entropy_init(mbedtls_entropy_context *);
void __real_mbedtls_entropy_free(mbedtls_entropy_context *);
void __real_mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *);
void __real_mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *);
int __real_mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *,
    int (*)(void *, unsigned char *, size_t), void *, const unsigned char *, size_t);
int __real_mbedtls_ctr_drbg_random(void *, unsigned char *, size_t);

void __wrap_mbedtls_entropy_init(mbedtls_entropy_context *p){
    ++entropy_live;++entropy_init_count;__real_mbedtls_entropy_init(p);
}
void __wrap_mbedtls_entropy_free(mbedtls_entropy_context *p){
    if(entropy_live<=0)abort();
    --entropy_live;++entropy_free_count;__real_mbedtls_entropy_free(p);
}
void __wrap_mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *p){
    ++drbg_live;++drbg_init_count;__real_mbedtls_ctr_drbg_init(p);
}
void __wrap_mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *p){
    if(drbg_live<=0)abort();
    --drbg_live;++drbg_free_count;__real_mbedtls_ctr_drbg_free(p);
}
int __wrap_mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *p,
    int (*f)(void *, unsigned char *, size_t), void *ctx,
    const unsigned char *custom, size_t len){
    if(failure_mode==1){failure_mode=0;return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;}
    return __real_mbedtls_ctr_drbg_seed(p,f,ctx,custom,len);
}
int __wrap_mbedtls_ctr_drbg_random(void *p,unsigned char *out,size_t len){
    if(failure_mode==2){failure_mode=0;return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;}
    return __real_mbedtls_ctr_drbg_random(p,out,len);
}
void csp_rng_test_mode(const char *mode){
    failure_mode=!strcmp(mode,"seed-failure")?1:!strcmp(mode,"random-failure")?2:0;
}
void csp_rng_test_status(void){
    printf("{\"entropy_live\":%d,\"drbg_live\":%d,\"entropy_init\":%d,\"entropy_free\":%d,\"drbg_init\":%d,\"drbg_free\":%d}\n",
      entropy_live,drbg_live,entropy_init_count,entropy_free_count,drbg_init_count,drbg_free_count);
    fflush(stdout);
}
int csp_rng_test_clean(void){
    if(entropy_live||drbg_live){fprintf(stderr,"RNG lifetime failure: entropy=%d drbg=%d\n",entropy_live,drbg_live);return 0;}
    return 1;
}
