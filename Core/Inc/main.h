#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

void Error_Handler(void);
extern void UserSysTickHandler(void);

#ifdef __cplusplus
}
#endif
