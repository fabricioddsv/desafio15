#pragma once
#include "hardware/spi.h"

void lora_init(spi_inst_t *spi, uint cs_pin, uint rst_pin);
void lora_set_frequency(long frequency);
void lora_send_packet(uint8_t *data, int length);
void lora_set_rx_mode(void);
int lora_receive_packet(uint8_t *buffer, int size);
uint8_t lora_read_reg(uint8_t reg); // Para debug
