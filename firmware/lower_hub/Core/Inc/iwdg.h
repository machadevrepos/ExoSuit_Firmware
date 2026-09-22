/* USER CODE BEGIN Header */
/**
  * @file    iwdg.h
  * @brief   Independent watchdog configuration for the lower hub.
  */
/* USER CODE END Header */
#ifndef __IWDG_H__
#define __IWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void MX_IWDG_Init(void);
void exo_lower_hub_iwdg_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __IWDG_H__ */
