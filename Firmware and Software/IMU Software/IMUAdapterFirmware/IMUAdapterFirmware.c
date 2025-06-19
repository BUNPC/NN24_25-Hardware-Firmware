// ---  NinjaNIRS 2025  ---
// Firmware for NN22_IMU_Adapter01_s00 board
//
// Initial version: 2024-04-10
// Bernhard Zimmermann - bzim@bu.edu
// Boston University Neurophotonics Center
//

// This firmware will display the output of an ISM330DHCX 6DOF IMU connected
// to a QWIIC port on the Control Board.
// To see the output, connect a terminal to the virtual COM port of the Pico USB.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "imu_adapter_pins_consts.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "uart_rx.pio.h"
#include "hardware/i2c.h"


uint16_t temp_dig;
uint16_t vimu_dig;
uint16_t imu_buf[N_EXT_IMUS+1][N_WORDS_PER_IMU]; // +1 for internal IMU

uint uart_rx_program_offset0;
uint uart_rx_program_offset1;

// function declarations
void refresh_imu_buf();
void refresh_adc_buf();
void strobe_imu_rx_all();
int int_acc_set_self_test(int8_t st_val);
int read_int_acc_raw(uint8_t res_buf[14]);
int init_int_acc();

int main() {
	const uint8_t PKT_HEADER[] = {253, 251};
	const uint8_t STATUS_PKT_HEADER[] = {239, 237};
	const uint8_t FIRMWARE_VERSION[] = {0, 0};
	const uint8_t PKT_LENGTH = 28; //make pkt len same as detector card for simplicity

	int ret = 0;
	uint ii, jj, iw;

	bool end_cyc_last = 0;
	bool end_cyc_curr;
	uint8_t cyc_cnt = 0;

	bool int_acc_connected;

	pico_unique_board_id_t board_id;

	/* PLL settings calculator:
	$ python pll_divs.py 96
	Requested: 96.0 MHz
	Achieved: 96.0 MHz
	FBDIV: 128 (VCO = 1536 MHz)
	PD1: 4
	PD2: 4
	*/
	clock_stop(clk_peri);
	clock_configure(clk_sys,
					CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
					0,
					12 * MHZ,
					12 * MHZ);
	pll_deinit(pll_sys);
	pll_init(pll_sys, 1, 1536 * MHZ, 4, 4);
	clock_configure(clk_sys,
					CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
					CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
					96 * MHZ,
					96 * MHZ);
	clock_configure(clk_peri,
					0,
					CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
					96 * MHZ,
					96 * MHZ);

	//stdio_init_all();

	// GPIO setup
	gpio_init(END_CYC_PIN); //input
	gpio_init(STATUS_SELECT_PIN); //input
	gpio_init(RESERVED_PIN);
	gpio_set_dir(RESERVED_PIN, GPIO_OUT);
	gpio_init(VIMU_EN_PIN);
	gpio_set_dir(VIMU_EN_PIN, GPIO_OUT);
	gpio_put(VIMU_EN_PIN, 0);
	gpio_init(LED_R_PIN);
	gpio_set_dir(LED_R_PIN, GPIO_OUT);
	gpio_init(LED_G_PIN);
	gpio_set_dir(LED_G_PIN, GPIO_OUT);
	gpio_put(LED_G_PIN, 1);

	// UART to IMU boards
	for (ii = 0; ii < N_EXT_IMUS; ii++){
		gpio_init(IMU_RX_PINS[ii]);
		gpio_set_dir(IMU_RX_PINS[ii], GPIO_OUT);
		gpio_put(IMU_RX_PINS[ii], 0);
	}

	// UART to FPGA on Control Board setup
	uart_init(uart0, 6000000);
	uart_set_hw_flow(uart0, true, false); // enable CTS (tx flow control)
	gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
	gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
	gpio_set_function(BOARD_SELECT_PIN, GPIO_FUNC_UART);
	
	// Internal ADC setup
	adc_init();
    adc_set_temp_sensor_enabled(true);
	adc_gpio_init(VIMU_SENSE_PIN);
    adc_select_input(TEMP_SENSE_ACH); // select temperature sensor
	temp_dig = adc_read();

	// Unique board ID - read from flash
	pico_get_unique_board_id(&board_id);

	// I2C initialisation. Using it at 200Khz.
    i2c_init(ACC_I2C, 200*1000);
    gpio_set_function(INT_IMU_SDA_PIN GPIO_FUNC_I2C);
    gpio_set_function(INT_IMU_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(INT_IMU_SDA_PIN);
    gpio_pull_up(INT_IMU_SCL_PIN);

	// sleep for a random (but constant for each board) time
	sleep_us(((uint64_t)board_id.id[6]<<5) + 512);

	// turn on power to IMUs
	sleep_ms(5); // give extra time to properly reset IMU MCUs
	gpio_put(VIMU_EN_PIN, 1);

	// UART can power IMU through IO, so only set to 1 here
	for (ii = 0; ii < N_EXT_IMUS; ii++){
		gpio_put(IMU_RX_PINS[ii], 1);
	}
	sleep_ms(2);

	// PIO setup
	uart_rx_program_offset0 = pio_add_program(pio0, &uart_rx_program);
	uart_rx_program_offset1 = pio_add_program(pio1, &uart_rx_program);
	for(ii = 0; ii<4; ii++) {
		uart_rx_program_init(pio0, ii, uart_rx_program_offset0, IMU_TX_PINS[ii], IMU_BAUD_RATE);
	}
	for(ii = 0; ii<(N_EXT_IMUS-4); ii++) {
		uart_rx_program_init(pio1, ii, uart_rx_program_offset1, IMU_TX_PINS[ii+4], IMU_BAUD_RATE);
	}

	// Initialize internal accelerometer
	int_acc_connected = false;
	for(ii = 0; ii<3; ii++) {
		ret = init_acc();
		if (res==PICO_ERROR_GENERIC){ // Accelerometer not connected or not replying.
			sleep_us(100);
		} else {
			int_acc_connected = true;
			break;
		}
	}

	gpio_put(LED_G_PIN, 0);
	//uart_puts(uart0, "init\n");
	
	while (1) { // Main Loop
		end_cyc_curr = gpio_get(END_CYC_PIN);
		if (end_cyc_curr == 1 && end_cyc_last == 0) {
			if (gpio_get(STATUS_SELECT_PIN)) {
				// write status packet into uart fifo
				uart_write_blocking(uart0, STATUS_PKT_HEADER, 2);
				uart_putc_raw(uart0, (char)cyc_cnt);
				uart_write_blocking(uart0, board_id.id, 8);
				uart_write_blocking(uart0, FIRMWARE_VERSION, 2);
				uart_write_blocking(uart0, (uint8_t *) &temp_dig, 2);
				uart_putc_raw(uart0, 0); // future additional status info
				uart_putc_raw(uart0, 0); 
				uart_putc_raw(uart0, 0); 
				uart_putc_raw(uart0, 0); 
				uart_putc_raw(uart0, 0); 
				temp_dig = adc_read();
			} else {
				// write results packet into uart fifo
				uart_write_blocking(uart0, PKT_HEADER, 2);
				uart_putc_raw(uart0, (char)cyc_cnt);
				if (imu_buf[cyc_cnt%8][0] == (0xFF|0x0E<<8)) { // check for correct header of 255 and 14
					uart_write_blocking(uart0, (uint8_t *) &imu_buf[cyc_cnt%8][1], 14);
					// ToDo: Check or retransmit checksum
					// make packet lenght same as detector card
					for (ii = 0; ii < (PKT_LENGTH - 2-1-14-1); ii++){
						uart_putc_raw(uart0, 0);
					}
				} else { // transmit zeroes if IMU not connected 
					for (ii = 0; ii < (PKT_LENGTH - 2-1-1); ii++){
						uart_putc_raw(uart0, 0);
					}
				}
				uart_putc_raw(uart0, 0); //possibly tx crc in future
			}

			if (cyc_cnt%8 == 7) {
				refresh_imu_buf();
			}

			cyc_cnt += 1;
		}
		end_cyc_last = end_cyc_curr;

		gpio_put(RESERVED_PIN, 1); // for measuring loop execution duration
		asm volatile("nop"); //asm volatile("nop \n nop");
		gpio_put(RESERVED_PIN, 0);
	}
}

void refresh_imu_buf(){
	// This function will first read the IMU PIO FIFOs, then send a pulse to all IMUs to get the next sample.

	// clear header
	// if no data is received, the header will not be correct, and zeros will instead be sent
	for(uint ii = 0; ii<N_EXT_IMUS; ii++){
		imu_buf[ii][0] = 0;
	}

	for(uint ii = 0; ii<4; ii++) {
		if (pio_sm_is_rx_fifo_full(pio0, ii)) { // sucessfull IMU read should fill the 8-word fifo
			for(uint iw = 0; iw<N_WORDS_PER_IMU-1; iw++) {
				imu_buf[ii][iw] = ((uint16_t *) &pio0->rxf[ii])[1];
			}
			// IMU unit sends odd number of bytes, also last one doesn't fit in FIFO, but is stored in ISR
			// after reading above, the FIFO should now have space, so manually push
			pio_sm_exec(pio0, ii , pio_encode_jmp(uart_rx_program_offset0 + uart_rx_offset_uart_push));
			sleep_us(1); // give PIO time to push into FIFO
			imu_buf[ii][N_WORDS_PER_IMU-1] = ((uint8_t *) &pio0->rxf[ii])[3];
		}
		if (pio_sm_is_rx_fifo_full(pio1, ii)) {
			for(uint iw = 0; iw<N_WORDS_PER_IMU-1; iw++) {
				imu_buf[ii+4][iw] = ((uint16_t *) &pio1->rxf[ii])[1];
			}
			pio_sm_exec(pio1, ii , pio_encode_jmp(uart_rx_program_offset1 + uart_rx_offset_uart_push));
			sleep_us(1);
			imu_buf[ii+4][N_WORDS_PER_IMU-1] = ((uint8_t *) &pio1->rxf[ii])[3];
		}
	}

	// flush fifos (just to make sure)
	for(uint ii = 0; ii<4; ii++) {
		pio_sm_clear_fifos(pio0, ii);
		pio_sm_clear_fifos(pio1, ii);
	}
	// request data from all IMUs
	strobe_imu_rx_all();
	// new data should arrive within 1.2 ms and will be ready when this function will be called next after 8 states
	return;
}

void refresh_adc_buf(){
	adc_select_input(TEMP_SENSE_ACH); // select temperature sensor
	temp_dig = adc_read();
	adc_select_input(VIMU_SENSE_ACH); // select voltage divider
	vimu_dig = adc_read();
	if (vimu_dig<2500) { // IMU voltage <~4V, fuse tripped?
		for (uint ii = 0; ii<4; ii++){
			pio_sm_set_enabled(pio0, ii, false);
			pio_sm_set_enabled(pio1, ii, false);
		}
		gpio_put(VIMU_EN_PIN, 0);
		// UART can power IMU through IO, so only set to 0 here
		for (uint ii = 0; ii < N_EXT_IMUS; ii++){
			gpio_put(IMU_RX_PINS[ii], 0);
			gpio_pull_down(IMU_TX_PINS[ii]);
		}
	}
	return;
}

void strobe_imu_rx_all(){
	for (uint ii = 0; ii < N_EXT_IMUS; ii++){
		gpio_put(IMU_RX_PINS[ii], 0);
	}
	sleep_us(1000000/IMU_BAUD_RATE);
	for (uint ii = 0; ii < N_EXT_IMUS; ii++){
		gpio_put(IMU_RX_PINS[ii], 1);
	}
}

int int_acc_set_self_test(int8_t st_val)
{
    // this function enables the accelerometer and gyro self test
    // st_val = -1: negative self test (both gyro and acc)
    // st_val = 0: self_test off
    // st_val = 1: positive self test (both gyro and acc)

    uint8_t buf[2];

    buf[0] = ACC_CTRL5_C_ADDR;
    if (st_val==-1) { // negative self-test
        buf[1] = 0b00001110;
    } else if (st_val==1) { // positive self-test
        buf[1] = 0b00000101;
    } else { // normal mode (self-test off)
        buf[1] = 0;
    }
    i2c_write_blocking(ACC_I2C, ACC_ADDR, buf, 2, false);
    return 0;
}

int read_int_acc_raw(uint8_t res_buf[14])
{
    // this function reads temperature, accelerometer, and gyro data into res_buf
    i2c_write_blocking(ACC_I2C, ACC_ADDR, &ACC_OUT_TEMP_L_ADDR, 1, true);
    return i2c_read_blocking(ACC_I2C, ACC_ADDR, res_buf, 14, false);
}

int init_int_acc()
{
    // this fuction resets and initializes the ISM330DHCX IMU

    int res = 0;
    uint8_t buf[3];

    // check whether "who am i" register is ok
    res = i2c_write_blocking(ACC_I2C, ACC_ADDR, &ACC_WHO_AM_I_ADDR, 1, true);
    if (res==PICO_ERROR_GENERIC) { // acc not replying to I2C address/command
        return PICO_ERROR_GENERIC;
    } else {
        i2c_read_blocking(ACC_I2C, ACC_ADDR, buf, 1, false);
        if (buf[0]!=ACC_WHO_AM_I_VAL){ // not the correct model connected
            return PICO_ERROR_GENERIC;
        }
    }  

    // reset the acc
    buf[0] = ACC_CTRL3_C_ADDR;
    buf[1] = ACC_CTRL3_C_VAL;
    i2c_write_blocking(ACC_I2C, ACC_ADDR, buf, 2, false);
    sleep_us(100);

    // config acc
    buf[0] = ACC_CTRL1_XL_ADDR;
    buf[1] = ACC_CTRL1_XL_VAL;
    buf[2] = ACC_CTRL2_G_VAL;
    i2c_write_blocking(ACC_I2C, ACC_ADDR, buf, 3, false);

    //read back and verify
    buf[0]=0;
    buf[1]=0;
    i2c_write_blocking(ACC_I2C, ACC_ADDR, &ACC_CTRL1_XL_ADDR, 1, true);
    i2c_read_blocking(ACC_I2C, ACC_ADDR, buf, 2, false);
    if (buf[0]!=ACC_CTRL1_XL_VAL || buf[1]!=ACC_CTRL2_G_VAL){
        return PICO_ERROR_GENERIC;
    }

    return 0;
}