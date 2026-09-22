/* The BLE central state machine is shared with U9.  This translation unit
 * selects the U11 ownership range and the Lower Hub transport hooks while
 * keeping the WPAN callbacks in the local app_ble.cpp adapter. */
#define EXO_HUB_LOWER_BUILD 1
#define EXO_HUB_LEAF_FIRST_NODE_ID 7U
#define EXO_HUB_OWNING_HUB exo::HubId::Lower

#include "../../../master/Core/Src/ble/exo_hub_central_client.cpp"
