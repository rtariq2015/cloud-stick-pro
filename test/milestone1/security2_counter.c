// Verbatim corrected SDK byte-counter helper, tested on deliberately unaligned
// byte storage and every carry boundary including wrap to the reserved zero.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sec2_gcm_iv_counter_increment.patched.inc"
int main(void) {
    uint8_t storage[16]; uint8_t *iv=storage+1;
    uint32_t values[]={0,1,0xfe,0xff,0xffff,0xffffff,0xfffffffe,0xffffffff};
    for (unsigned i=0;i<sizeof values/sizeof *values;++i) {
        memset(storage,0xa5,sizeof storage);
        uint32_t before=values[i],after=before+1;
        iv[8]=before>>24;iv[9]=before>>16;iv[10]=before>>8;iv[11]=before;
        sec2_gcm_iv_counter_increment(iv);
        assert(iv[8]==(uint8_t)(after>>24) && iv[9]==(uint8_t)(after>>16));
        assert(iv[10]==(uint8_t)(after>>8) && iv[11]==(uint8_t)after);
        assert(storage[0]==0xa5 && storage[13]==0xa5);
        for (unsigned j=0;j<8;++j) assert(iv[j]==0xa5);
    }
    puts("Security2 counter: 8 unaligned carry/wrap cases preserve IV encoding/session bytes");
}
