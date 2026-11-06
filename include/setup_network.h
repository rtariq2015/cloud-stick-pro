#pragma once
#include <IPAddress.h>
#include <cstdint>
bool setup_network_load();
bool setup_network_save(uint32_t address);
IPAddress setup_network_ip();
bool setup_network_client(const IPAddress& ip);
