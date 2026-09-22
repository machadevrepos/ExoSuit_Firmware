/* USER CODE BEGIN Header */
/**
  * @file    iwdg.c
  * @brief   Independent watchdog configuration for the lower hub.
  */
/* USER CODE END Header */
#include "iwdg.h"

void MX_IWDG_Init(void)
{
  /* LSI is already enabled by SystemClock_Config/RTC. A /64 prescaler and a
   * 2047 reload give approximately four seconds at the nominal 32 kHz LSI. */
  IWDG->KR = 0x5555U;
  IWDG->PR = 4U;
  IWDG->RLR = 2047U;
  while ((IWDG->SR & 0x3U) != 0U) {
  }
  IWDG->KR = 0xCCCCU;
}

void exo_lower_hub_iwdg_refresh(void)
{
  IWDG->KR = 0xAAAAU;
}
