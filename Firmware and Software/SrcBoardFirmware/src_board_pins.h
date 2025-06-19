// ---  NinjaNIRS 2024  ---
// Pin constants for NN22_VCCS_SrcAdapter02_s00 board
//
// Initial version: 2024-03-26
// Bernhard Zimmermann - bzim@bu.edu
// Boston University Neurophotonics Center
//

// Backplane pins: GPIO00 - GPIO06
const uint UART_TX_PIN = 0;
const uint UART_RX_PIN = 1;
const uint BOARD_SELECTN_PIN = 2;
//const uint STEP_PIN = 3; // defined in led_sel_fsm.pio
const uint LED_EN_IN_PIN = 4;
const uint STATUS_SELECT_PIN = 5;
const uint RESERVED_PIN = 6;

// Misc Pins
const uint LED_R_PIN = 8;
const uint LED_G_PIN = 7;
const uint SPARE_PINS[] = {19, 20};

// DAC pins
const uint CSN_DAC_PIN = 21;
const uint SCLK_DAC_PIN = 22;
const uint MOSI_DAC_PIN = 23;
const uint RESETN_DAC_PIN = 24;
const uint LDACN_DAC_PIN = 18;

// LED (Source optode) control pins
const uint VLED_EN_PIN = 10;
const uint LED_AD_PINS[] = {12, 13, 14, 15};
const uint LED_ENN_PIN = 16;
const uint DUMMY_EN_PIN = 17;
 
// Analog sense pins
const uint LED_CURR_SENSE_PIN = 26;
const uint LED_CURR_SENSE_ACH = 0;
const uint VIN_SENSE_PIN = 27;
const uint VIN_SENSE_ACH = 1;
const uint VNEG_SENSE_PIN = 28;
const uint VNEG_SENSE_ACH = 2;
const uint LED_COM_SENSE_PIN = 29;
const uint LED_COM_SENSE_ACH = 3;
const uint TEMP_SENSE_ACH = 4;

