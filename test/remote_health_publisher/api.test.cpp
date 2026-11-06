#include "webdav_client.h"
#include <type_traits>
template<class T,class=void>struct HasHealth:std::false_type{};
template<class T>struct HasHealth<T,std::void_t<decltype(&T::publishHealth)>>:std::true_type{};
static_assert(HasHealth<WebDAVClient>::value==bool(CSP_ENABLE_REMOTE_HEALTH));
int main(){return 0;}
