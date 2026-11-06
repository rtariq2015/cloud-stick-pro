#include "local_dashboard.h"
extern "C" {
unsigned char dashboard_server_layout[sizeof(pro::local_dashboard::Server)];
unsigned char dashboard_caller_buffer_layout[pro::local_dashboard::HeaderLimit+1+640+pro::local_dashboard::StatusLimit];
unsigned char dashboard_passive_flag[CSP_DASHBOARD_PASSIVE_CLOSE+1];
}
