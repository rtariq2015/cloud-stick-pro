#pragma once
#include "platform.h"
#define socket owned_fake_socket
#define ioctl owned_fake_ioctl
#define bind owned_fake_bind
#define close owned_fake_close
#define recvfrom owned_fake_recvfrom
#define sendto owned_fake_sendto
