#include "dma_driver.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <iostream>

unsigned int write_dma(unsigned int *virtual_addr, int offset, unsigned int value)
{
    virtual_addr[offset>>2] = value;
    return 0;
}

unsigned int read_dma(unsigned int *virtual_addr, int offset)
{
    return virtual_addr[offset>>2];
}

void send_via_dma(uint8_t* data, size_t size) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[DMA SEND] ERROR: open /dev/mem");
        std::cerr << "[DMA SEND] Failed to open /dev/mem for sending" << std::endl;
        return;
    }
    unsigned int *dma_virtual_addr = (unsigned int *)mmap(NULL, 65535, PROT_READ | PROT_WRITE, MAP_SHARED, fd, AXI_LITE_ADDR);
    if (dma_virtual_addr == MAP_FAILED) {
        perror("[DMA SEND] ERROR: mmap dma registers");
        std::cerr << "[DMA SEND] Failed to mmap DMA control registers" << std::endl;
        close(fd);
        return;
    }
    unsigned int *src_virtual_addr = (unsigned int *)mmap(NULL, size + 1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SOURCE_ADDR);
    if (src_virtual_addr == MAP_FAILED) {
        perror("[DMA SEND] ERROR: mmap source address");
        std::cerr << "[DMA SEND] Failed to mmap source buffer, size=" << size << std::endl;
        munmap(dma_virtual_addr, 65535);
        close(fd);
        return;
    }
    memcpy(src_virtual_addr, data, size);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
    while (!(read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER) & STATUS_IDLE));
    write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
    write_dma(dma_virtual_addr, MM2S_TRNSFR_LENGTH_REGISTER, size);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA);
    while (!(read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER) & STATUS_IDLE));
    munmap(src_virtual_addr, size + 1000);
    munmap(dma_virtual_addr, 65535);
    close(fd);
}

void receive_via_dma(uint8_t* dst, size_t size) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[DMA RECV] ERROR: open /dev/mem");
        std::cerr << "[DMA RECV] Failed to open /dev/mem for receiving" << std::endl;
        return;
    }

    unsigned int *dma_virtual_addr = (unsigned int *)mmap(NULL, 65535, PROT_READ | PROT_WRITE, MAP_SHARED, fd, AXI_LITE_ADDR);
    if (dma_virtual_addr == MAP_FAILED) {
        perror("[DMA RECV] ERROR: mmap dma registers");
        std::cerr << "[DMA RECV] Failed to mmap DMA control registers" << std::endl;
        close(fd);
        return;
    }

    unsigned int *dst_virtual_addr = (unsigned int *)mmap(NULL, size + 1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DESTINATION_ADDR);
    if (dst_virtual_addr == MAP_FAILED) {
        perror("[DMA RECV] ERROR: mmap destination address");
        std::cerr << "[DMA RECV] Failed to mmap destination buffer, size=" << size << std::endl;
        munmap(dma_virtual_addr, 65535);
        close(fd);
        return;
    }

    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RESET_DMA);
    while (!(read_dma(dma_virtual_addr, S2MM_STATUS_REGISTER) & STATUS_IDLE));
    write_dma(dma_virtual_addr, S2MM_DST_ADDRESS_REGISTER, DESTINATION_ADDR);
    write_dma(dma_virtual_addr, S2MM_BUFF_LENGTH_REGISTER, size);
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA);
    while (!(read_dma(dma_virtual_addr, S2MM_STATUS_REGISTER) & STATUS_IDLE));

    uint8_t* dst_bytes = reinterpret_cast<uint8_t*>(dst_virtual_addr);
    memcpy(dst, dst_bytes, size);

    munmap(dst_virtual_addr, size + 1000);
    munmap(dma_virtual_addr, 65535);
    close(fd);
}