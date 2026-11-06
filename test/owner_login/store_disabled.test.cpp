#include "owner_login_feature.h"
#include "owner_login_nvs.h"
#include <owner_login_store.h>
#include <iostream>
static_assert(CSP_ENABLE_OWNER_LOGIN==0,"Default production owner-login feature must be disabled");
int main(){std::cout<<"PASS default-off storage/adapter link without owner mutation or hash implementation\n";}
