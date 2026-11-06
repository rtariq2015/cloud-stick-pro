/* Host-only test adapter around unchanged Espressif Security2/SRP implementation.
 * No radio, USB, file, NVS or networking access. Synthetic credentials only.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocomm_security2.h"
#include "esp_srp.h"
#include "synthetic_fixture.h"

static int from_hex(const char *hex, uint8_t *out, size_t cap) {
    size_t n=strlen(hex);if(n%2 || n/2>cap)return -1;
    for(size_t i=0;i<n;i+=2){unsigned int x;if(sscanf(hex+i,"%2x",&x)!=1)return -1;
        if(!((hex[i]>='0'&&hex[i]<='9')||(hex[i]>='a'&&hex[i]<='f')||(hex[i]>='A'&&hex[i]<='F')) ||
           !((hex[i+1]>='0'&&hex[i+1]<='9')||(hex[i+1]>='a'&&hex[i+1]<='f')||(hex[i+1]>='A'&&hex[i+1]<='F')))return -1;
        out[i/2]=(uint8_t)x;
    }return (int)(n/2);
}

#ifdef CSP_TEST_RNG_LIFETIME
void csp_rng_test_mode(const char *);
void csp_rng_test_status(void);
int csp_rng_test_clean(void);
#endif

int main(int argc, char **argv) {
#ifdef CSP_TEST_RNG_LIFETIME
    csp_rng_test_mode(argc>1?argv[1]:"");
#else
    (void)argc;(void)argv;
#endif
    protocomm_security2_params_t params={
        .salt=(const char *)host_sec2_salt,.salt_len=sizeof host_sec2_salt,
        .verifier=(const char *)host_sec2_verifier,.verifier_len=sizeof host_sec2_verifier};
    protocomm_security_handle_t handle=NULL;
    if(protocomm_security2.init(&handle)!=ESP_OK || protocomm_security2.new_transport_session(handle,1)!=ESP_OK)return 3;
    char line[4096];uint8_t in[1024];
    while(fgets(line,sizeof line,stdin)) {
        line[strcspn(line,"\r\n")]=0;
        char *payload=strchr(line,' ');if(payload)*payload++=0;else payload="";
        uint8_t *out=NULL;ssize_t outlen=0;int ret=ESP_ERR_INVALID_ARG;
        int len=from_hex(payload,in,sizeof in);
#ifdef CSP_TEST_RNG_LIFETIME
        if(!strcmp(line,"rng-status")){csp_rng_test_status();continue;}
#endif
        if(!strcmp(line,"session") && len>0 && len<=1024)
            ret=protocomm_security2.security_req_handler(handle,&params,1,in,len,&out,&outlen,NULL);
        else if(!strcmp(line,"decrypt") && len>=16 && len<=1024)
            ret=protocomm_security2.decrypt(handle,1,in,len,&out,&outlen);
        else if(!strcmp(line,"encrypt") && len>=0 && len<=1008)
            ret=protocomm_security2.encrypt(handle,1,in,len,&out,&outlen);
        else if(!strcmp(line,"reset")) {
            ret=protocomm_security2.close_transport_session(handle,1);
            if(ret==ESP_OK)ret=protocomm_security2.new_transport_session(handle,1);
        } else if(!strcmp(line,"quit"))break;
        printf("{\"error\":%d,\"data\":\"",ret);
        if(ret==ESP_OK && out && outlen>=0 && outlen<4096)for(ssize_t i=0;i<outlen;i++)printf("%02x",out[i]);
        printf("\"}\n");fflush(stdout);free(out);
    }
    protocomm_security2.cleanup(handle);
#ifdef CSP_TEST_RNG_LIFETIME
    return csp_rng_test_clean()?0:90;
#else
    return 0;
#endif
}
