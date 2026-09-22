# Lower Hub CubeMX seed

`LowerHub.ioc` is the CubeMX seed for the second STM32WB55 on the
dual-controller master PCB. The CubeMX-generated project is now present in this
directory as the clean hardware baseline. Runtime firmware integration is a
separate step and has not started yet.

The seed keeps the existing Master clock, RF, STM32_WPAN, HSEM/IPCC, RTC,
sequencer, low-power, SWD, and debug-console setup, while removing the local
sensor, SD/FATFS, SPI, timer, actuator, and unused GPIO initialization.

The IOC intent and hardware baseline are:

- STM32WB55CCU6, STM32Cube FW_WB V1.24.0, 6 BLE central links
- MTU 247, DLE enabled, 2M PHY negotiation remains application-controlled
- USART1 asynchronous 921600 baud on PB6/PB7
- USART1 DMA1 Channel 2 RX circular and Channel 3 TX
- USART1 IRQ enabled for idle-line/error handling; PA8/USART1_CK is unused
- PB3 configured as the SWO trace output (`SYS_JTDO-SWO`)
- LPUART1 remains available at 115200 for diagnostics
- BLE identity `HUB0002`, central-only role, no local peripheral service

The generated WPAN application files still contain CubeMX's default template
values (`BLE_CFG_PERIPHERAL=1`, `BLE_CFG_CENTRAL=0`, two links, and MTU 156).
They must be replaced or updated during the lower-hub firmware integration
step before this project is flashed as U11. This baseline commit intentionally
does not begin that firmware work.

LPUART1 is not part of the bridge transport. It is retained as an optional
diagnostic console inherited from the original Master project; SWO on PB3 is
the preferred trace path for the lower hub.

Open `LowerHub.ioc` in CubeMX and regenerate into this directory only when the
IOC changes. Do not regenerate `firmware/master/Master.ioc`; the two
configurations are separate projects. The generated pin, DMA, memory-map, and
interrupt output has been reviewed for this baseline.
