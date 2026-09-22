#ifndef EXO_MASTER_BRIDGE_H_
#define EXO_MASTER_BRIDGE_H_

#include <stdint.h>

#include "stm32wbxx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors exo::bridge::LinkState (link_health.h): 0 Absent, 1 Booting,
 * 2 Healthy, 3 Congested, 4 Restarted. */
typedef struct {
    uint8_t link_state;
    uint32_t link_age_ms;
    uint32_t restart_count;
    uint32_t frame_count;
    uint32_t bad_frame_count;
    uint32_t queue_live_count;
    uint32_t queue_reliable_count;
    uint32_t overwrite_count;
    uint32_t reject_count;
} exo_master_bridge_diag_t;

void exo_master_bridge_init(void);
void exo_master_bridge_process(void);
uint8_t exo_master_bridge_send_blepipe(uint8_t msg_type,
                                       uint16_t src_id,
                                       uint16_t dst_id,
                                       const uint8_t *payload,
                                       uint16_t payload_len);
void exo_master_bridge_uart_error(UART_HandleTypeDef *uart);
void exo_master_bridge_get_diag(exo_master_bridge_diag_t *out);

#ifdef __cplusplus
}
#endif

#endif
