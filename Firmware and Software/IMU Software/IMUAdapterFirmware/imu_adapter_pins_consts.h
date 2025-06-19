// ---  NinjaNIRS 2025  ---
// Pin constants for NN22_IMU_Adapter01_s00 board
//
// Initial version: 2024-04-10
// Bernhard Zimmermann - bzim@bu.edu
// Boston University Neurophotonics Center
//


// Backplane pins: GPIO00 - GPIO06
const uint UART_TX_PIN = 0;
const uint UART_RX_PIN = 1;
const uint BOARD_SELECT_PIN = 2;
//const uint TRG_PIN = 3; // check also adcacq.pio
const uint END_CYC_PIN = 4;
const uint STATUS_SELECT_PIN = 5;
const uint RESERVED_PIN = 6;

// Misc Pins
const uint VIMU_EN_PIN = 10;
const uint LED_R_PIN = 8;
const uint LED_G_PIN = 7;
const uint VIMU_SENSE_PIN = 29;
const uint VIMU_SENSE_ACH = 3;

const uint TEMP_SENSE_ACH = 4;

// IMU pins
// RX: from IMU Adapter to IMU Board
// TX: from IMU Board to IMU Adapter
// (IMU Board centric labeling)
#define N_EXT_IMUS 7
const uint IMU_TX_PINS[] = {25, 17, 15, 19, 23, 27, 20};//{19, 21, 23, 27, 25, 15, 17, 13};
const uint IMU_RX_PINS[] = {24, 16, 14, 18, 22, 26, 21};//{18, 20, 22, 26, 24, 14, 16, 12};

const uint INT_IMU_SDA_PIN = 12;
const uint INT_IMU_SCL_PIN = 13;
#define INT_ACC_I2C i2c1

// IMU constants

// scaling factors and offsets
// const float ACC_ACCEL_SCF = 4.0/(2<<14);
// const float ACC_GYRO_SCF = 250.0/(2<<14);
// const float ACC_TEMP_SCF = 1.0/256;
// const float ACC_TEMP_OFFSET = 25.0;


const uint IMU_BAUD_RATE = 250000;
const uint N_BYTES_PER_IMU = 17;
#define N_WORDS_PER_IMU 9

// Address of ISM330DHCX 6DOF IMU
// 0x6A if SA0 = low, 0x6B if SA0 = high
// (SA0 pin on sparkfun board is pulled high by default)
const uint8_t ACC_ADDR= 0x6A;
const uint8_t ACC_WHO_AM_I_ADDR = 0x0F;
const uint8_t ACC_WHO_AM_I_VAL = 0x6B;
const uint8_t ACC_CTRL3_C_ADDR = 0x12; 
const uint8_t ACC_CTRL3_C_VAL = 0x03; // software reset
const uint8_t ACC_CTRL1_XL_ADDR = 0x10;
const uint8_t ACC_CTRL1_XL_VAL = 0b01111000; //accel ODR (833Hz) and FS (+-4g)
const uint8_t ACC_CTRL2_G_VAL = 0b01110000; //gyro ODR (833Hz) and FS (+-250dps)
const uint8_t ACC_OUT_TEMP_L_ADDR = 0x20; //start of output registers
const uint8_t ACC_CTRL5_C_ADDR = 0x14;