/*
Copyright (c) 2014-2017 Banana Pi
Updates Copyright (C) 2022 akuker

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "hal/pi_defs/bpi-gpio.h"

/* The following define the mapping of the Banana Pi CPU pins to the logical
 * GPIO numbers. The GPIO numbers are used by the software to set/configure
 * the CPU registers. */
#define BPI_M2P_01 -1
#define BPI_M2P_03 GPIO_PA12
#define BPI_M2P_05 GPIO_PA11
#define BPI_M2P_07 GPIO_PA06
#define BPI_M2P_09 -1
#define BPI_M2P_11 GPIO_PA01
#define BPI_M2P_13 GPIO_PA00
#define BPI_M2P_15 GPIO_PA03
#define BPI_M2P_17 -1
#define BPI_M2P_19 GPIO_PC00
#define BPI_M2P_21 GPIO_PC01
#define BPI_M2P_23 GPIO_PC02
#define BPI_M2P_25 -1
#define BPI_M2P_27 GPIO_PA19
#define BPI_M2P_29 GPIO_PA07
#define BPI_M2P_31 GPIO_PA08
#define BPI_M2P_33 GPIO_PA09
#define BPI_M2P_35 GPIO_PA10
#define BPI_M2P_37 GPIO_PA17
#define BPI_M2P_39 -1

#define BPI_M2P_02 -1
#define BPI_M2P_04 -1
#define BPI_M2P_06 -1
#define BPI_M2P_08 GPIO_PA13
#define BPI_M2P_10 GPIO_PA14
#define BPI_M2P_12 GPIO_PA16
#define BPI_M2P_14 -1
#define BPI_M2P_16 GPIO_PA15
#define BPI_M2P_18 GPIO_PC04
#define BPI_M2P_20 -1
#define BPI_M2P_22 GPIO_PA02
#define BPI_M2P_24 GPIO_PC03
#define BPI_M2P_26 GPIO_PC07
#define BPI_M2P_28 GPIO_PA18
#define BPI_M2P_30 -1
#define BPI_M2P_32 GPIO_PL02
#define BPI_M2P_34 -1
#define BPI_M2P_36 GPIO_PL04
#define BPI_M2P_38 GPIO_PA21
#define BPI_M2P_40 GPIO_PA20

const Banana_Pi_Gpio_Mapping banana_pi_m2p_map{
    .phys_to_gpio_map =
        {
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_03, BPI_M2P_03},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_05, BPI_M2P_05},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_07, BPI_M2P_07},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_08, BPI_M2P_08},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_10, BPI_M2P_10},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_11, BPI_M2P_11},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_12, BPI_M2P_12},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_13, BPI_M2P_13},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_15, BPI_M2P_15},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_16, BPI_M2P_16},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_18, BPI_M2P_18},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_19, BPI_M2P_19},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_21, BPI_M2P_21},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_22, BPI_M2P_22},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_23, BPI_M2P_23},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_24, BPI_M2P_24},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_26, BPI_M2P_26},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_27, BPI_M2P_27},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_28, BPI_M2P_28},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_29, BPI_M2P_29},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_31, BPI_M2P_31},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_32, BPI_M2P_32},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_33, BPI_M2P_33},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_35, BPI_M2P_35},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_36, BPI_M2P_36},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_37, BPI_M2P_37},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_38, BPI_M2P_38},
            {board_type::pi_physical_pin_e::PI_PHYS_PIN_40, BPI_M2P_40},
        },

    .I2C_DEV    = "/dev/i2c-0",
    .SPI_DEV    = "/dev/spidev0.0",
    .I2C_OFFSET = 2,
    .SPI_OFFSET = 3,
    .PWM_OFFSET = 3,
};

//map phys_num(index) to bp gpio_num(element)
 int physToGpio_BPI_M2P [64] =
{
          -1,                //0
          -1,        -1,     //1, 2
   BPI_M2P_03,        -1,     //3, 4
   BPI_M2P_05,        -1,     //5, 6
   BPI_M2P_07, BPI_M2P_08,     //7, 8
          -1, BPI_M2P_10,     //9, 10
   BPI_M2P_11, BPI_M2P_12,     //11, 12
   BPI_M2P_13,        -1,     //13, 14
   BPI_M2P_15, BPI_M2P_16,     //15, 16
          -1, BPI_M2P_18,     //17, 18
   BPI_M2P_19,        -1,     //19, 20
   BPI_M2P_21, BPI_M2P_22,     //21, 22
   BPI_M2P_23, BPI_M2P_24,     //23, 24
          -1, BPI_M2P_26,     //25, 26
   BPI_M2P_27, BPI_M2P_28,     //27, 28
   BPI_M2P_29,        -1,     //29, 30
   BPI_M2P_31, BPI_M2P_32,     //31, 32      
   BPI_M2P_33,        -1,     //33, 34
   BPI_M2P_35, BPI_M2P_36,     //35, 36
   BPI_M2P_37, BPI_M2P_38,     //37, 38
          -1, BPI_M2P_40,     //39, 40
   -1,   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, //41-> 55
   -1,   -1, -1, -1, -1, -1, -1, -1 // 56-> 63
} ;