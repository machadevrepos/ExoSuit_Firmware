#ifndef EXO_MASTER_BRIDGE_H_
#define EXO_MASTER_BRIDGE_H_

#include <stdint.h>

#include "stm32wbxx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void exo_master_bridge_init(void);
void exo_master_bridge_process(void);
uint8_t exo_master_bridge_send_blepipe(uint8_t msg_type,
                                       uint16_t src_id,
                                       uint16_t dst_id,
                                       const uint8_t *payload,
                                       uint16_t payload_len);
void exo_master_bridge_uart_error(UART_HandleTypeDef *uart);

#ifdef __cplusplus
}
#endif

#endif
