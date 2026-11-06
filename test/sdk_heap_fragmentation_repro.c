/* Offline exercise of the exact pinned TLSF source. Synthetic pool only.
 * Host pointer size differs from Xtensa; this demonstrates fragmentation,
 * not the identity/geometry of the remaining physical target allocation. */
#include "tlsf.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct snapshot {size_t free_bytes,largest;unsigned free_blocks,used_blocks;};
static bool visit(void* ptr,size_t size,int used,void* arg){
    (void)ptr;struct snapshot* s=arg;
    if(used)++s->used_blocks;
    else {++s->free_blocks;s->free_bytes+=size;if(size>s->largest)s->largest=size;}
    return true;
}
static struct snapshot sample(tlsf_t t){
    struct snapshot s={0};tlsf_walk_pool(tlsf_get_pool(t),visit,&s);
    return s;
}
int main(void){
    void* arena=malloc(256*1024);assert(arena);
    tlsf_t t=tlsf_create_with_pool(arena,256*1024,0);assert(t);
    struct snapshot before=sample(t);
    void* transient=tlsf_malloc(t,32768);assert(transient);
    void* retained=tlsf_malloc(t,16);assert(retained);
    void* other=tlsf_malloc(t,65536);assert(other);
    tlsf_free(t,transient);tlsf_free(t,other);
    struct snapshot after=sample(t);
    assert(after.used_blocks==1&&after.free_blocks==2);
    assert(before.free_bytes-after.free_bytes<128);
    size_t delta=tlsf_fit_size(t,before.largest)-tlsf_fit_size(t,after.largest);
    assert(delta>=32768&&delta<=40960);
    printf("16-byte retained allocation: total free delta=%zu; allocable-largest delta=%zu; raw-largest delta=%zu\n",
           before.free_bytes-after.free_bytes,delta,before.largest-after.largest);
    tlsf_free(t,retained);
    struct snapshot final=sample(t);
    assert(final.used_blocks==0&&final.free_blocks==1&&final.free_bytes==before.free_bytes&&final.largest==before.largest);
    tlsf_destroy(t);free(arena);
    puts("PASS exact pinned TLSF: small retained metadata can split a much larger free span; final release coalesces fully");
}
