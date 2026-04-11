#include "dma_driver.hpp"

unsigned int *virtual_dst_addr;
unsigned int *virtual_src_addr;
unsigned int *dma_virtual_addr;

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

    if (status & STATUS_HALTED) {
		printf(" Halted.\n");
	} else {
		printf(" Running.\n");
	}

    if (status & STATUS_IDLE) {
		printf(" Idle.\n");
	}

    if (status & STATUS_SG_INCLDED) {
		printf(" SG is included.\n");
	}

    if (status & STATUS_DMA_INTERNAL_ERR) {
		printf(" DMA internal error.\n");
	}

    if (status & STATUS_DMA_SLAVE_ERR) {
		printf(" DMA slave error.\n");
	}

    if (status & STATUS_DMA_DECODE_ERR) {
		printf(" DMA decode error.\n");
	}

    if (status & STATUS_SG_INTERNAL_ERR) {
		printf(" SG internal error.\n");
	}

    if (status & STATUS_SG_SLAVE_ERR) {
		printf(" SG slave error.\n");
	}

    if (status & STATUS_SG_DECODE_ERR) {
		printf(" SG decode error.\n");
	}

    if (status & STATUS_IOC_IRQ) {
		printf(" IOC interrupt occurred.\n");
	}

    if (status & STATUS_DELAY_IRQ) {
		printf(" Interrupt on delay occurred.\n");
	}

    if (status & STATUS_ERR_IRQ) {
		printf(" Error interrupt occurred.\n");
	}
}

unsigned int dma_mm2s_status(unsigned int *virtual_addr)
{
    unsigned int status = read_dma(virtual_addr, MM2S_STATUS_REGISTER);

    printf("Memory-mapped to stream status (0x%08x@0x%02x):", status, MM2S_STATUS_REGISTER);

    if (status & STATUS_HALTED) {
		printf(" Halted.\n");
	} else {
		printf(" Running.\n");
	}

    if (status & STATUS_IDLE) {
		printf(" Idle.\n");
	}

    if (status & STATUS_SG_INCLDED) {
		printf(" SG is included.\n");
	}

    if (status & STATUS_DMA_INTERNAL_ERR) {
		printf(" DMA internal error.\n");
	}

    if (status & STATUS_DMA_SLAVE_ERR) {
		printf(" DMA slave error.\n");
	}

    if (status & STATUS_DMA_DECODE_ERR) {
		printf(" DMA decode error.\n");
	}

    if (status & STATUS_SG_INTERNAL_ERR) {
		printf(" SG internal error.\n");
	}

    if (status & STATUS_SG_SLAVE_ERR) {
		printf(" SG slave error.\n");
	}

    if (status & STATUS_SG_DECODE_ERR) {
		printf(" SG decode error.\n");
	}

    if (status & STATUS_IOC_IRQ) {
		printf(" IOC interrupt occurred.\n");
	}

    if (status & STATUS_DELAY_IRQ) {
		printf(" Interrupt on delay occurred.\n");
	}

    if (status & STATUS_ERR_IRQ) {
		printf(" Error interrupt occurred.\n");
	}
	return status;
}

int dma_mm2s_sync(unsigned int *virtual_addr)
{
    unsigned int mm2s_status =  read_dma(virtual_addr, MM2S_STATUS_REGISTER);

	// sit in this while loop as long as the status does not read back 0x00001002 (4098)
	// 0x00001002 = IOC interrupt has occured and DMA is idle
	while(!(mm2s_status & IOC_IRQ_FLAG) || !(mm2s_status & IDLE_FLAG))
	{
        mm2s_status =  read_dma(virtual_addr, MM2S_STATUS_REGISTER);
        usleep(50);
    }

	return 0;
}

int dma_s2mm_sync(unsigned int *virtual_addr)
{
    unsigned int s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);

	// sit in this while loop as long as the status does not read back 0x00001002 (4098)
	// 0x00001002 = IOC interrupt has occured and DMA is idle
	while(!(s2mm_status & IOC_IRQ_FLAG) || !(s2mm_status & IDLE_FLAG))
	{
        s2mm_status = read_dma(virtual_addr, S2MM_STATUS_REGISTER);
        usleep(50);
    }

	return 0;
}

void print_mem(void *virtual_address, int byte_count)
{
	char *data_ptr = (char*)virtual_address;

	for(int i=0;i<byte_count;i++){
		printf("%02X", data_ptr[i]);

		// print a space every 4 bytes (0 indexed)
		if(i%4==3){
			printf(" ");
		}
	}

	printf("\n");
}
unsigned int send_message(const unsigned char* buffer, size_t length)
{
    if (buffer == nullptr) {
        return 1;
    }

    if (length != 16) {
        return 2;
    }

    // Wait for previous transfer to finish
    while (!(read_dma(dma_virtual_addr, MM2S_STATUS_REGISTER) & IDLE_FLAG)) {
    }

    // Clear old sticky completion/error bits
    write_dma(dma_virtual_addr,
              MM2S_STATUS_REGISTER,
              STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);

    // Copy 16 bytes into the fixed MM2S source buffer
    memcpy((void*)virtual_src_addr, buffer, 16);

    // Only needed here if SOURCE_ADDR is not already programmed in dma_init()
    write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);

    // Only needed here if MM2S is not already left running by dma_init()
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA);

    // Start transfer: 16 bytes = 128 bits
    // In direct-register mode, LENGTH must be written last.
    write_dma(dma_virtual_addr, MM2S_TRNSFR_LENGTH_REGISTER, 16);

    // Wait until transfer completes
    dma_mm2s_sync(dma_virtual_addr);

    return 0;
}
unsigned int receive_message(unsigned char* buffer, size_t length) // receives exactly one 16-byte message
{
    if (buffer == nullptr) {
        return 1;
    }

    if (length != 16) {
        return 2;
    }

    // Wait until previous S2MM transfer is finished
    while (!(read_dma(dma_virtual_addr, S2MM_STATUS_REGISTER) & IDLE_FLAG)) {
    }

    // Clear old sticky completion/error bits
    write_dma(dma_virtual_addr,
              S2MM_STATUS_REGISTER,
              STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);

    // Optional for debugging only
    memset((void*)virtual_dst_addr, 0, 16);

    // Reprogram destination address if you want to be explicit
    write_dma(dma_virtual_addr, S2MM_DST_ADDRESS_REGISTER, DESTINATION_ADDR);

    // Make sure S2MM is running
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA);

    // Arm receive for exactly 16 bytes
    // In direct-register mode, writing LENGTH starts the transfer
    write_dma(dma_virtual_addr, S2MM_BUFF_LENGTH_REGISTER, 16);

    // Wait until the AXI stream side has delivered the message into memory
    dma_s2mm_sync(dma_virtual_addr);

    // Copy received 16 bytes out to caller's buffer
    memcpy(buffer, (void*)virtual_dst_addr, 16);

    return 0;
}
int dma_init(void)
{

    //printf("Hello World! - Running DMA transfer test application.\n");

	//printf("Opening a character device file of the Ultra96's DDR memeory...\n");
	int ddr_memory = open("/dev/mem", O_RDWR | O_SYNC);

	//printf("Memory map the address of the DMA AXI IP via its AXI lite control interface register block.\n");
    dma_virtual_addr = (unsigned int *) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, AXI_LITE_ADDR);

	//printf("Memory map the MM2S source address register block.\n");
    virtual_src_addr  = (unsigned int*) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, SOURCE_ADDR);

	//printf("Memory map the S2MM destination address register block.\n");
    virtual_dst_addr = (unsigned int *) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_memory, DESTINATION_ADDR);

	//printf("Writing random data to source register block...\n");
	virtual_src_addr[0]= 0xEFBEADDE;
	virtual_src_addr[1]= 0x11223344;
	virtual_src_addr[2]= 0xABABABAB;
	virtual_src_addr[3]= 0xCDCDCDCD;
	virtual_src_addr[4]= 0x00001111;
	virtual_src_addr[5]= 0x22223333;
	virtual_src_addr[6]= 0x44445555;
	virtual_src_addr[7]= 0x66667777;

	//printf("Clearing the destination register block...\n");
    memset(virtual_dst_addr, 0, 32);

    //printf("Source memory block data:      ");
	//print_mem(virtual_src_addr, 32);

    //printf("Destination memory block data: ");
	//print_mem(virtual_dst_addr, 32);

    //printf("Reset the DMA.\n");
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RESET_DMA);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

	//printf("Halt the DMA.\n");
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, HALT_DMA);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, HALT_DMA);
    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

	//printf("Enable all interrupts.\n");
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, ENABLE_ALL_IRQ);
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, ENABLE_ALL_IRQ);
    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

    //printf("Writing source address of the data from MM2S in DDR...\n");
    write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
    dma_mm2s_status(dma_virtual_addr);

    //printf("Writing the destination address for the data from S2MM in DDR...\n");
    write_dma(dma_virtual_addr, S2MM_DST_ADDRESS_REGISTER, DESTINATION_ADDR);
    dma_s2mm_status(dma_virtual_addr);

	//printf("Run the MM2S channel.\n");
    write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA);
    dma_mm2s_status(dma_virtual_addr);

	//printf("Run the S2MM channel.\n");
    write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA);
    dma_s2mm_status(dma_virtual_addr);

    dma_s2mm_status(dma_virtual_addr);
    dma_mm2s_status(dma_virtual_addr);

   // printf("Destination memory block: ");
   // print_mem(virtual_dst_addr, DMA_TRANSFER_SIZE);

	//printf("\n");

    return 0;
}
