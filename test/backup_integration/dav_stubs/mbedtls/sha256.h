#pragma once
#include <openssl/evp.h>
#include <cstddef>
struct mbedtls_sha256_context {EVP_MD_CTX* ctx=nullptr;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* p){p->ctx=EVP_MD_CTX_new();}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* p,int){return EVP_DigestInit_ex(p->ctx,EVP_sha256(),nullptr)==1?0:-1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* p,const unsigned char* b,size_t n){return EVP_DigestUpdate(p->ctx,b,n)==1?0:-1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* p,unsigned char* out){unsigned n=0;return EVP_DigestFinal_ex(p->ctx,out,&n)==1&&n==32?0:-1;}
inline void mbedtls_sha256_free(mbedtls_sha256_context* p){EVP_MD_CTX_free(p->ctx);p->ctx=nullptr;}
inline int mbedtls_sha256(const unsigned char* b,size_t n,unsigned char* out,int){unsigned len=0;return EVP_Digest(b,n,out,&len,EVP_sha256(),nullptr)==1&&len==32?0:-1;}
