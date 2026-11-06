#pragma once
#define ESP_RETURN_ON_FALSE(condition, value, ...) do { if (!(condition)) return (value); } while(0)
