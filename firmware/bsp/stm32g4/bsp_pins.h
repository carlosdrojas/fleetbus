#pragma once
/* Nucleo-G431RB pin map. FROM THE DATASHEET/UM2505, NOT YET VERIFIED ON A BOARD. Check each against the schematic.
 *   LED LD2 .......... PA5          CAN1 RX/TX ....... PA11 / PA12 (AF9) -> SN65HVD230 RXD/TXD
 *   LPUART1 (VCP) .... PA2 / PA3    I2C1 SCL/SDA ..... PB8 / PB9 (AF4)   -> INA219
 *   ADC1_IN1 / IN2 ... PA0 / PA1    (thermistor divider / pot standing in for pack voltage)
 *   Servo PWM ........ PB4 (TIM3_CH1, AF2)   Head / turn lights ... PB0 / PB1 */
