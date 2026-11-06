#include "logger.h"
#include <stdarg.h>
#include <USB.h>
#include <USBCDC.h>
void log_init() {} // Logging never touches removable storage.
void stick_log_printf(const char* format,...) {
  char buf[256];va_list args;va_start(args,format);vsnprintf(buf,sizeof buf,format,args);va_end(args);
  if(USBSerial)USBSerial.print(buf);
}
