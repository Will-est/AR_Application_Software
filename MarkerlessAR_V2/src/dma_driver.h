#ifndef DMA_DRIVER_H
#define DMA_DRIVER_H

#include <cstdint>
#include <cstddef>

#define AXI_LITE_ADDR 0xA0000000
#define MM2S_CONTROL_REGISTER       0x00
#define MM2S_STATUS_REGISTER        0x04
#define MM2S_SRC_ADDRESS_REGISTER   0x18
#define MM2S_TRNSFR_LENGTH_REGISTER 0x28
#define S2MM_CONTROL_REGISTER       0x30
#define S2MM_STATUS_REGISTER        0x34
#define S2MM_DST_ADDRESS_REGISTER   0x48
#define S2MM_BUFF_LENGTH_REGISTER   0x58
#define STATUS_IDLE                 0x00000002
#define RUN_DMA                     0x00000001
#define RESET_DMA                   0x00000004
#define SOURCE_ADDR                 0x0e000000
#define DESTINATION_ADDR            0x0f000000

void send_via_dma(uint8_t* data, size_t size);
void receive_via_dma(uint8_t* dst, size_t size);

#endif // DMA_DRIVER_H