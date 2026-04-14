#include "dma_driver.hpp"
#include <errno.h>
#include <time.h>
#include <stdlib.h>

unsigned int *virtual_dst_addr;
unsigned int *virtual_src_addr;
unsigned int *dma_virtual_addr;
unsigned int *accel_virtual_addr;

namespace
{
uint64_t now_mono_ms()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

int getTimeoutMs()
{
    static int cached = -2;
    if (cached != -2)
        return cached;

    const char* raw = getenv("AR_DMA_MSG_TIMEOUT_MS");
    if (!raw || !*raw)
    {
        cached = DMA_DEFAULT_TIMEOUT_MS; // use hardcoded default, env var can still override
        return cached;
    }

    const int parsed = atoi(raw);
    cached = parsed > 0 ? parsed : DMA_DEFAULT_TIMEOUT_MS;
    return cached;
}

bool statusHasError(unsigned int status)
{
    return (status & (STATUS_DMA_INTERNAL_ERR |
                      STATUS_DMA_SLAVE_ERR    |
                      STATUS_DMA_DECODE_ERR   |
                      STATUS_SG_INTERNAL_ERR  |
                      STATUS_SG_SLAVE_ERR     |
                      STATUS_SG_DECODE_ERR    |
                      STATUS_ERR_IRQ)) != 0;
}
} // namespace

unsigned int write_dma(unsigned int *virtual_addr, int offset, unsigned int value)
{
    virtual_addr[offset>>2] = value;
    return 0;
}

unsigned int read_dma(unsigned int *virtual_addr, int offset)
{
    return virtual_addr[offset>>2];
}

void dma_s2mm_status(unsigned int *virtual_addr)
{
    unsigned int status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);
    printf("Stream to memory-mapped status (0x%08x@0x%02x):", status, S2MM_STATUS_REGISTER);
    if (status & STATUS_HALTED)        printf(" Halted.\n");   else printf(" Running.\n");
    if (status & STATUS_IDLE)          printf(" Idle.\n");
    if (status & STATUS_SG_INCLDED)    printf(" SG is included.\n");
    if (status & STATUS_DMA_INTERNAL_ERR) printf(" DMA internal error.\n");
    if (status & STATUS_DMA_SLAVE_ERR) printf(" DMA slave error.\n");
    if (status & STATUS_DMA_DECODE_ERR) printf(" DMA decode error.\n");
    if (status & STATUS_SG_INTERNAL_ERR) printf(" SG internal error.\n");
    if (status & STATUS_SG_SLAVE_ERR)  printf(" SG slave error.\n");
    if (status & STATUS_SG_DECODE_ERR) printf(" SG decode error.\n");
    if (status & STATUS_IOC_IRQ)       printf(" IOC interrupt occurred.\n");
    if (status & STATUS_DELAY_IRQ)     printf(" Interrupt on delay occurred.\n");
    if (status & STATUS_ERR_IRQ)       printf(" Error interrupt occurred.\n");
}

unsigned int dma_mm2s_status(unsigned int *virtual_addr)
{
    unsigned int status = read_dma(virtual_addr, MM2S_STATUS_REGISTER);
    printf("Memory-mapped to stream status (0x%08x@0x%02x):", status, MM2S_STATUS_REGISTER);
    if (status & STATUS_HALTED)        printf(" Halted.\n");   else printf(" Running.\n");
    if (status & STATUS_IDLE)          printf(" Idle.\n");
    if (status & STATUS_SG_INCLDED)    printf(" SG is included.\n");
    if (status & STATUS_DMA_INTERNAL_ERR) printf(" DMA internal error.\n");
    if (status & STATUS_DMA_SLAVE_ERR) printf(" DMA slave error.\n");
    if (status & STATUS_DMA_DECODE_ERR) printf(" DMA decode error.\n");
    if (status & STATUS_SG_INTERNAL_ERR) printf(" SG internal error.\n");
    if (status & STATUS_SG_SLAVE_ERR)  printf(" SG slave error.\n");
    if (status & STATUS_SG_DECODE_ERR) printf(" SG decode error.\n");
    if (status & STATUS_IOC_IRQ)       printf(" IOC interrupt occurred.\n");
    if (status & STATUS_DELAY_IRQ)     printf(" Interrupt on delay occurred.\n");
    if (status & STATUS_ERR_IRQ)       printf(" Error interrupt occurred.\n");
    return status;
}

int dma_mm2s_sync(unsigned int *virtual_addr)
{
    unsigned int mm2s_status = read_dma(virtual_addr, MM2S_STATUS_REGISTER);
    unsigned int s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);

    const int timeoutMs = getTimeoutMs();
    const uint64_t deadline = (timeoutMs > 0) ? (now_mono_ms() + static_cast<uint64_t>(timeoutMs)) : 0ULL;

    while (!(mm2s_status & IOC_IRQ_FLAG) || !(mm2s_status & IDLE_FLAG))
    {
        if (statusHasError(mm2s_status))
        {
            printf("[DMA] mm2s_sync: error MM2S=0x%08x\n", mm2s_status);
            return EIO;
        }
        if (timeoutMs > 0 && now_mono_ms() > deadline)
        {
            s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);
            printf("[DMA] mm2s_sync: TIMED OUT MM2S=0x%08x S2MM=0x%08x\n", mm2s_status, s2mm_status);
            dma_s2mm_status(virtual_addr);
            dma_mm2s_status(virtual_addr);
            return ETIMEDOUT;
        }
        mm2s_status = read_dma(virtual_addr, MM2S_STATUS_REGISTER);
    }

    return 0;
}

int dma_s2mm_sync(unsigned int *virtual_addr)
{
    unsigned int s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);

    const int timeoutMs = getTimeoutMs();
    const uint64_t deadline = (timeoutMs > 0) ? (now_mono_ms() + static_cast<uint64_t>(timeoutMs)) : 0ULL;

    while (!(s2mm_status & IOC_IRQ_FLAG) || !(s2mm_status & IDLE_FLAG))
    {
        if (statusHasError(s2mm_status))
        {
            printf("[DMA] s2mm_sync: error in status 0x%08x\n", s2mm_status);
            return EIO;
        }
        if (timeoutMs > 0 && now_mono_ms() > deadline)
        {
            printf("[DMA] s2mm_sync: TIMED OUT, last status = 0x%08x\n", s2mm_status);
            dma_s2mm_status(virtual_addr);
            dma_mm2s_status(virtual_addr);
            return ETIMEDOUT;
        }
        s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);
    }

    return 0;
}

void print_mem(void *virtual_address, int byte_count)
{
    char *data_ptr = (char*)virtual_address;
    for (int i = 0; i < byte_count; i++)
    {
        printf("%02X", data_ptr[i]);
        if (i % 4 == 3) printf(" ");
    }
    printf("\n");
}

unsigned int send_message(const unsigned char* buffer, size_t length)
{
    if (buffer == nullptr) return 1;
    if (length != 16) return 2;
    if (!dma_virtual_addr || !virtual_src_addr) return ENODEV;

    static int msgCount = 0;

    auto reset_mm2s = [&]()
    {
        write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
        write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
        write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
        write_dma(dma_virtual_addr, MM2S_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
    };

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        unsigned int mm2s_status = read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER);

        if (mm2s_status & STATUS_IOC_IRQ)
        {
            const int timeoutMs = getTimeoutMs();
            const uint64_t deadline = (timeoutMs > 0) ? (now_mono_ms() + static_cast<uint64_t>(timeoutMs)) : 0ULL;
            while (!(read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER) & IDLE_FLAG))
            {
                if (timeoutMs > 0 && now_mono_ms() > deadline)
                {
                    printf("[DMA] send_message: timed out waiting for idle\n");
                    if (attempt == 0 && read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER) == 0x00000000)
                    {
                        reset_mm2s();
                        continue;
                    }
                    return ETIMEDOUT;
                }
            }
            write_dma(dma_virtual_addr, MM2S_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
        }

        memcpy((void*)virtual_src_addr, buffer, 16);

        write_dma(dma_virtual_addr, MM2S_TRNSFR_LENGTH_REGISTER, 16);
        const int rc = dma_mm2s_sync(dma_virtual_addr);
        if (rc == 0)
        {
            msgCount++;
            return 0;
        }

        const unsigned int afterStatus = read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER);
        if (attempt == 0 && afterStatus == 0x00000000)
        {
            reset_mm2s();
            continue;
        }

        return static_cast<unsigned int>(rc);
    }

    return EIO;
}

unsigned int receive_message(unsigned char* buffer, size_t length)
{
    if (buffer == nullptr) return 1;
    if (length != 16) return 2;

    unsigned int s2mm_status = read_dma(dma_virtual_addr, S2MM_STATUS_REGISTER);

    // Only wait for idle if a previous transfer completed
    // On first call IOC_IRQ is not set so we skip straight to the transfer
    if (s2mm_status & STATUS_IOC_IRQ)
    {
        const int timeoutMs = getTimeoutMs();
        const uint64_t deadline = (timeoutMs > 0) ? (now_mono_ms() + static_cast<uint64_t>(timeoutMs)) : 0ULL;
        while (!(read_dma(dma_virtual_addr, S2MM_STATUS_REGISTER) & IDLE_FLAG))
        {
            if (timeoutMs > 0 && now_mono_ms() > deadline)
            {
                printf("[DMA] receive_message: timed out waiting for idle\n");
                return ETIMEDOUT;
            }
        }
        write_dma(dma_virtual_addr, S2MM_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
    }

    memset((void*)virtual_dst_addr, 0, 16);
    //write_dma(dma_virtual_addr, S2MM_DST_ADDRESS_REGISTER, DESTINATION_ADDR);
    //write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);

    write_dma(dma_virtual_addr, S2MM_BUFF_LENGTH_REGISTER, 16);
    const int rc = dma_s2mm_sync(dma_virtual_addr);
    if (rc != 0) return static_cast<unsigned int>(rc);

    memcpy(buffer, (void*)virtual_dst_addr, 16);
    return 0;
}

int dma_init(void)
{
    int ddr_memory = open("/dev/mem", O_RDWR | O_SYNC);
    if (ddr_memory < 0)
    {
        perror("[DMA] open /dev/mem failed");
        return -1;
    }

    dma_virtual_addr = (unsigned int*) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, (off_t)AXI_LITE_ADDR);
    if (dma_virtual_addr == MAP_FAILED) { perror("[DMA] mmap dma_virtual_addr failed"); return -1; }
    printf("[DMA] dma_virtual_addr mapped OK\n");

    virtual_src_addr = (unsigned int*) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, (off_t)SOURCE_ADDR);
    if (virtual_src_addr == MAP_FAILED) { perror("[DMA] mmap virtual_src_addr failed"); return -1; }
    printf("[DMA] virtual_src_addr mapped OK\n");

    virtual_dst_addr = (unsigned int*) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, (off_t)DESTINATION_ADDR);
    if (virtual_dst_addr == MAP_FAILED) { perror("[DMA] mmap virtual_dst_addr failed"); return -1; }
    printf("[DMA] virtual_dst_addr mapped OK\n");

    accel_virtual_addr = (unsigned int*) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, (off_t)ACCELERATOR_AXI_LITE_ADR);
    if (accel_virtual_addr == MAP_FAILED) { perror("[DMA] mmap accel_virtual_addr failed"); return -1; }
    printf("[DMA] accel_virtual_addr mapped OK\n");


    // Write known pattern to src so we can verify MM2S is reading real data
    // virtual_src_addr[0] = 0xEFBEADDE;
    // virtual_src_addr[1] = 0x11223344;
    // virtual_src_addr[2] = 0xABABABAB;
    // virtual_src_addr[3] = 0xCDCDCDCD;
    // virtual_src_addr[4] = 0x00001111;
    // virtual_src_addr[5] = 0x22223333;
    // virtual_src_addr[6] = 0x44445555;
    // virtual_src_addr[7] = 0x66667777;
    memset(virtual_dst_addr, 0, 32);
    memset(virtual_src_addr, 0, 32);


    printf("[DMA] init: resetting DMA\n");
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RESET_DMA);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

    printf("[DMA] init: halting DMA\n");
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, HALT_DMA);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, HALT_DMA);
    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

    printf("[DMA] init: enabling interrupts and running\n");
    // fixed: was ENABLE_ALL_IRQ only, which left run bit unset
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

    printf("[DMA] init: setting addresses\n");
    write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
    dma_mm2s_status(dma_virtual_addr);

    write_dma(dma_virtual_addr, S2MM_DST_ADDRESS_REGISTER, DESTINATION_ADDR);
    dma_s2mm_status(dma_virtual_addr);

    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);
    // Arm S2MM once and leave it running so accelerator output drains freely.
    write_dma(dma_virtual_addr, S2MM_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
    write_dma(dma_virtual_addr, S2MM_BUFF_LENGTH_REGISTER, MMAP_SIZE);


    printf("[DMA] init complete\n");
    return 0;
}
