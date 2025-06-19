// ---  NinjaNIRS 2022  ---
// Firmware for NN22_RPi_DetAdapter00_s02_R66R67 board
//
// Major update (status packet): 2023-03-30
// Initial version: 2022-12-13 (DetBoardTest00)
// Bernhard Zimmermann - bzim@bu.edu
// Boston University Neurophotonics Center
//

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "src_board_pins.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "led_sel_fsm.pio.h"

void process_cmd();
void enable_led_fsms(uint16_t led_sel_state, uint16_t dac_state);
void disable_led_fsms();
static inline void put_dac(uint16_t dac_val);

// Global Constants
#define N_BYTES_IN_CMD 7
const uint8_t STATUS_PKT_HEADER[] = {239, 238};
const uint8_t FIRMWARE_VERSION[] = {0, 0};
const uint16_t DAC_TRANS_VAL = 100; // dac value during state transition
const uint32_t TRANS_DELAY = 1000; // delay in ~10ns increments between DAC_TRANS_VAL and final value
const uint SAFE_LED_SEL_STATE = 0b110000; // led off, dummy off
const uint MAN_STAT_UNLOCK_BIT = 8;
const uint MAN_STAT_EN_BIT = 9;
const uint MAN_STAT_CLR_BIT = 10;
const PIO pio = pio0;
const uint sm_lsel = 0;
const uint sm_dac = 1;

// Global Variables
pico_unique_board_id_t board_id;
uint16_t dac_state_ram[1024];
uint16_t led_sel_state_ram[1024];
uint16_t man_dac_state = 0;
uint16_t man_led_sel_state = SAFE_LED_SEL_STATE;
uint32_t stat_reg = 0;
uint8_t uart_buf[N_BYTES_IN_CMD];

uint led_sel_fsm_offset;
uint led_dac_fsm_offset;

// Main Program
int main() {
	int ret = 0;
	uint ii;

	bool led_en_in_last = 0;
	bool led_en_in_curr;
	bool step_last = 0;
	bool step_curr;
	uint16_t state_ram_addr = 0;
	uint16_t next_state = 0;
	uint8_t n_bytes_rxd = 0;


	for (ii=0; ii<1024; ii++){
		dac_state_ram[ii] = 0;
		led_sel_state_ram[ii] = SAFE_LED_SEL_STATE; 
	}

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

	// GPIO setup
	gpio_init(STEP_PIN); //input
	gpio_init(LED_EN_IN_PIN); //input
	gpio_init(STATUS_SELECT_PIN); //input
	gpio_init(RESERVED_PIN);
	gpio_set_dir(RESERVED_PIN, GPIO_OUT);
	gpio_init(LED_R_PIN); // indicator leds on pcb
	gpio_set_dir(LED_R_PIN, GPIO_OUT);
	gpio_put(LED_R_PIN, 1);
	gpio_init(LED_G_PIN);
	gpio_set_dir(LED_G_PIN, GPIO_OUT);

	// LED control pin setup
	gpio_init(VLED_EN_PIN);
	gpio_set_dir(VLED_EN_PIN, GPIO_OUT);
	gpio_put(VLED_EN_PIN, 0);
	gpio_init(LED_ENN_PIN);
	gpio_set_dir(LED_ENN_PIN, GPIO_OUT);
	gpio_put(LED_ENN_PIN, 1);
	// LED_ENN_PIN controlled by led_sel_fsm when LED_EN_IN_PIN is asserted

	// DAC setup
	gpio_init(RESETN_DAC_PIN);
	gpio_set_dir(RESETN_DAC_PIN, GPIO_OUT);
	gpio_put(RESETN_DAC_PIN, 0);
	gpio_init(LDACN_DAC_PIN);
	gpio_set_dir(LDACN_DAC_PIN, GPIO_OUT);
	gpio_put(LDACN_DAC_PIN, 1);
	// SPI controlled by led_dac_fsm pio

	// UART setup
	uart_init(uart0, 6000000);
	uart_set_hw_flow(uart0, true, false); // enable CTS (tx flow control)
	gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
	gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
	gpio_set_function(BOARD_SELECTN_PIN, GPIO_FUNC_UART); // still readable by main pgm
	
	// PIO setup
	led_sel_fsm_offset = pio_add_program(pio, &led_sel_fsm_program);
	led_sel_fsm_program_init(pio, sm_lsel, led_sel_fsm_offset, DUMMY_EN_PIN, LED_AD_PINS[0], TRANS_DELAY);
	led_dac_fsm_offset = pio_add_program(pio, &led_dac_fsm_program);
	led_dac_fsm_program_init(pio, sm_dac, led_dac_fsm_offset, CSN_DAC_PIN, SCLK_DAC_PIN, 
			MOSI_DAC_PIN, (uint32_t)DAC_TRANS_VAL<<12 | 0b0011<<28);

	// ADC setup
	adc_init();
    adc_set_temp_sensor_enabled(true);
	adc_gpio_init(LED_CURR_SENSE_PIN);
	adc_gpio_init(VIN_SENSE_PIN);
	adc_gpio_init(VNEG_SENSE_PIN);
	adc_gpio_init(LED_COM_SENSE_PIN);

	// Unique board ID - read from flash
	pico_get_unique_board_id(&board_id);

	// sleep for a random (but constant for each board) time
	sleep_us(((uint64_t)board_id.id[6]<<5) + 1024);

	// turn on power
	gpio_put(VLED_EN_PIN, 1);
	sleep_us(150); // wait for voltage regulator turn-on
	gpio_put(LED_R_PIN, 0);
	
	while (1) { // Main Loop

		led_en_in_curr = gpio_get(LED_EN_IN_PIN);
		step_curr = gpio_get(STEP_PIN);

		if (!led_en_in_curr) { // acquisition not running in fpga
			if (led_en_in_last){ // just turned off
				disable_led_fsms();
			}
			// while acq not running we can processs commands
			while (uart_is_readable(uart0)) {
				uart_buf[n_bytes_rxd] = (uint8_t) uart_getc(uart0);
				n_bytes_rxd++;
				if (gpio_get(BOARD_SELECTN_PIN)) {
					// this board NOT selected, flush all received bytes
					n_bytes_rxd = 0;
				} else if (n_bytes_rxd == 1) {
					if (uart_buf[0] != 255) { // all commands must start with 255
						n_bytes_rxd = 0; // discard byte 
					}
				} else if (n_bytes_rxd == 3) {
					if ((uart_buf[2] & 0b01110000) != 0b00110000) { // check for valid address
						n_bytes_rxd = 0; // discard byte 
					}
				} else if (n_bytes_rxd == N_BYTES_IN_CMD) { // complete command received
					process_cmd();
					n_bytes_rxd = 0;
				}
			}
			// process manual state flags
			if (stat_reg & 1u<<MAN_STAT_EN_BIT) { // enable/update manual status
				enable_led_fsms(man_led_sel_state, man_dac_state);
				stat_reg &= ~(1u<<MAN_STAT_EN_BIT); // clear bit
			}
			if (stat_reg & 1u<<MAN_STAT_CLR_BIT) { // disable manual status
				disable_led_fsms();
				stat_reg &= ~(1u<<MAN_STAT_CLR_BIT); // clear bit
			}

		} else { // acquisition running, focus on feeding data to the DAC
			if (!led_en_in_last){ // just turned on
				next_state = 0;
				enable_led_fsms(led_sel_state_ram[next_state], dac_state_ram[next_state]);
				// queue next word in DAC & fsm fifo
				next_state++;
				put_dac(dac_state_ram[next_state]);
				pio_sm_put(pio, sm_lsel, (uint32_t)led_sel_state_ram[next_state]);
				next_state++;
			}

		 	if (step_curr && !step_last){ // step signal received; TO-DO: check FIFO contents instead
				// led_sel_fsm will have output new led select and DAC values
				// queue next word in dac
				put_dac(dac_state_ram[next_state]);
				// queue next word in fsm fifo
				pio_sm_put(pio, sm_lsel, (uint32_t)led_sel_state_ram[next_state]);
				// update state counter
				if (next_state < 1023 && !(led_sel_state_ram[next_state] & 1u<<15)) { // check end bit
					next_state++;
				} else {
					next_state = 0;
				}				
		 	}
		}

		led_en_in_last = led_en_in_curr;
		step_last = step_curr;
		gpio_put(RESERVED_PIN, 1); // for measuring loop execution duration
		asm volatile("nop"); //asm volatile("nop \n nop");
		gpio_put(RESERVED_PIN, 0);
	}
}

void process_cmd(){ // process command received via uart
	uint16_t temp_adc;
	bool write_cmd = uart_buf[2] & (1u<<7);
	uint16_t state_ram_addr = (((uint16_t)uart_buf[2] & 0b00001111)<<8) + (uint16_t)uart_buf[1];

	if (state_ram_addr < 1024) { // read/write to state ram
		if (write_cmd) {
			dac_state_ram[state_ram_addr] = (uint16_t)uart_buf[3] + ((uint16_t)uart_buf[4]<<8);
			led_sel_state_ram[state_ram_addr] = (uint16_t)uart_buf[5] + ((uint16_t)uart_buf[6]<<8);
		} else {
			// read ram and send to FPGA
			uart_buf[3] = dac_state_ram[state_ram_addr] & 0xFF;
			uart_buf[4] = (dac_state_ram[state_ram_addr]>>8) & 0xFF;
			uart_buf[5] = led_sel_state_ram[state_ram_addr] & 0xFF;
			uart_buf[6] = (led_sel_state_ram[state_ram_addr]>>8) & 0xFF;
			uart_write_blocking (uart0, uart_buf, N_BYTES_IN_CMD);
		}
	} else if (state_ram_addr == 0x0FF9) { // read/write manual led status
		if (write_cmd) {
			man_dac_state = (uint16_t)uart_buf[3] + ((uint16_t)uart_buf[4]<<8);
			man_dac_state &= 0x7FFF; // limit power to 1/2 max for safety
			man_led_sel_state = (uint16_t)uart_buf[5] + ((uint16_t)uart_buf[6]<<8);
		} else {
			// read man state and send to FPGA
			uart_buf[3] = man_dac_state & 0xFF;
			uart_buf[4] = (man_dac_state>>8) & 0xFF;
			uart_buf[5] = man_led_sel_state & 0xFF;
			uart_buf[6] = (man_led_sel_state>>8) & 0xFF;
			uart_write_blocking (uart0, uart_buf, N_BYTES_IN_CMD);
		}
	} else if (state_ram_addr == 0x0FFA) { // read ADC values of Curr and Vin
		if (!write_cmd) {
			adc_select_input(LED_CURR_SENSE_ACH);
			temp_adc = adc_read();
			uart_buf[3] = temp_adc & 0xFF;
			uart_buf[4] = (temp_adc>>8) & 0xFF;
			adc_select_input(VIN_SENSE_ACH);
			temp_adc = adc_read();
			uart_buf[5] = temp_adc & 0xFF;
			uart_buf[6] = (temp_adc>>8) & 0xFF;
			uart_write_blocking (uart0, uart_buf, N_BYTES_IN_CMD);
		}
	} else if (state_ram_addr == 0x0FFB) { // read ADC values of Vneg and LEDCom
		if (!write_cmd) {
			adc_select_input(VNEG_SENSE_ACH);
			temp_adc = adc_read();
			uart_buf[3] = temp_adc & 0xFF;
			uart_buf[4] = (temp_adc>>8) & 0xFF;
			adc_select_input(LED_COM_SENSE_ACH);
			temp_adc = adc_read();
			uart_buf[5] = temp_adc & 0xFF;
			uart_buf[6] = (temp_adc>>8) & 0xFF;
			uart_write_blocking (uart0, uart_buf, N_BYTES_IN_CMD);
		}
	} else if (state_ram_addr == 0x0FFC) { // read ADC value of Temperature
		if (!write_cmd) {
			adc_select_input(TEMP_SENSE_ACH);
			temp_adc = adc_read();
			uart_buf[3] = temp_adc & 0xFF;
			uart_buf[4] = (temp_adc>>8) & 0xFF;
			uart_buf[5] = 0;
			uart_buf[6] = 0;
			uart_write_blocking (uart0, uart_buf, N_BYTES_IN_CMD);
		}
	} else if (state_ram_addr == 0x0FFD) { // read lower 4 bytes of board id
		if (!write_cmd) { 
			uart_write_blocking(uart0, uart_buf, 3);
			uart_write_blocking(uart0, board_id.id, 4); 
		}
	} else if (state_ram_addr == 0x0FFE) { // read upper 4 bytes of board id
		if (!write_cmd) { 
			uart_write_blocking(uart0, uart_buf, 3);
			uart_write_blocking(uart0, board_id.id+4, 4); 
		}
	} else if (state_ram_addr == 0x0FFF) { // read/write status register word
		if (write_cmd) {
			uint32_t stat_reg_old = stat_reg;
			stat_reg = (uint32_t)uart_buf[3] + ((uint32_t)(uart_buf[4]&0b11111100)<<8)
				+ ((uint32_t)uart_buf[5]<<16) + ((uint32_t)uart_buf[6]<<24);
			// special handling for manual state unlock bit
			// not allowed to unlock and enable manual state simulataneously
			if ((uart_buf[4] & 0b11) == 0b01 || stat_reg_old & 1u << MAN_STAT_UNLOCK_BIT) {
				stat_reg |= 1u << MAN_STAT_UNLOCK_BIT;
				gpio_put(LED_G_PIN, 1); 
			}
			// enable manual state if unlocked
			if ((uart_buf[4] & 0b11) == 0b10 && stat_reg_old & 1u << MAN_STAT_UNLOCK_BIT) { 
				stat_reg |= 1u << MAN_STAT_EN_BIT;
				gpio_put(LED_R_PIN, 1); 
			}
		} else {
			uart_write_blocking(uart0, uart_buf, 3);
			uart_write_blocking(uart0, (uint8_t*)&stat_reg, 4);
		}
	}
}

void enable_led_fsms(uint16_t led_sel_state, uint16_t dac_state) {
	// release DAC
	gpio_put(RESETN_DAC_PIN, 1);
	sleep_us(90);
	// reset FSMs
	pio_set_sm_mask_enabled(pio, (1u << sm_lsel) | (1u << sm_dac), false); // make sure they're off
	pio_sm_exec(pio, sm_lsel , pio_encode_jmp(led_sel_fsm_offset)); // go to start of program
	pio_sm_exec(pio, sm_dac , pio_encode_jmp(led_dac_fsm_offset) | pio_encode_sideset(false, false));
	pio_sm_clear_fifos(pio, sm_lsel);
	pio_sm_clear_fifos(pio, sm_dac);
	pio_interrupt_clear(pio, TRANS_VAL_IRQ);
	pio_interrupt_clear(pio, FINAL_VAL_IRQ);
	pio_interrupt_clear(pio, ACQ_IRQ);
	pio_sm_exec(pio, sm_lsel, pio_encode_mov(pio_osr, pio_x));
	pio_sm_exec(pio, sm_lsel, pio_encode_out(pio_pins, 5)); // out safe value - LED_ENN high
	pio_restart_sm_mask(pio, (1u << sm_lsel) | (1u << sm_dac));
	// reload transition delay to isr (cleared by restart above)
	pio_sm_put(pio, sm_lsel, TRANS_DELAY);
	pio_sm_exec(pio, sm_lsel, pio_encode_pull(false, false));
	pio_sm_exec(pio, sm_lsel, pio_encode_mov(pio_isr, pio_osr));
	// queue first state
	pio_sm_put(pio, sm_lsel, led_sel_state);
	put_dac(dac_state);
	// give control of LED_ENN to fsm
	pio_gpio_init(pio, LED_ENN_PIN);
	// turn on FSMs
	pio_enable_sm_mask_in_sync(pio, (1u << sm_lsel) | (1u << sm_dac));
	// out first value
	pio_sm_exec(pio, sm_lsel, pio_encode_jmp(led_sel_fsm_offset + led_sel_fsm_offset_load_new_vals));
}

void disable_led_fsms() {
	// turn off both SMs
	pio_set_sm_mask_enabled(pio, (1u << sm_lsel) | (1u << sm_dac), false);
	// reset DAC so that selected current is 0
	gpio_put(RESETN_DAC_PIN, 0);
	asm volatile("nop \n nop");
	// take control of LED_ENN and deassert
	gpio_init(LED_ENN_PIN);
	gpio_set_dir(LED_ENN_PIN, GPIO_OUT);
	gpio_put(LED_ENN_PIN, 1);
}

static inline void put_dac(uint16_t dac_val) {
	// left align in pio fifo, 0b0011 will immediately update dac output
	pio_sm_put(pio, sm_dac, (uint32_t)dac_val<<12 | 0b0011<<28);
}