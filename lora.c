#include "lora.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"

static spi_inst_t *lora_spi;
static uint lora_cs_pin;
static uint lora_rst_pin;

static void lora_write_reg(uint8_t reg, uint8_t value) {
    gpio_put(lora_cs_pin, 0);
    uint8_t buf[2] = { reg | 0x80, value };
    spi_write_blocking(lora_spi, buf, 2);
    gpio_put(lora_cs_pin, 1);
}

uint8_t lora_read_reg(uint8_t reg) { // Tornar pública para debug
    gpio_put(lora_cs_pin, 0);
    uint8_t tx = reg & 0x7F;
    uint8_t rx = 0;
    spi_write_blocking(lora_spi, &tx, 1);
    spi_read_blocking(lora_spi, 0, &rx, 1);
    gpio_put(lora_cs_pin, 1);
    return rx;
}

void lora_init(spi_inst_t *spi, uint cs_pin, uint rst_pin) {
    lora_spi = spi;
    lora_cs_pin = cs_pin;
    lora_rst_pin = rst_pin;

    gpio_init(lora_rst_pin);
    gpio_set_dir(lora_rst_pin, GPIO_OUT);
    gpio_put(lora_rst_pin, 0);
    sleep_ms(10);
    gpio_put(lora_rst_pin, 1);
    sleep_ms(100); // Tempo maior para garantir reset

    gpio_init(lora_cs_pin);
    gpio_set_dir(lora_cs_pin, GPIO_OUT);
    gpio_put(lora_cs_pin, 1);

    // Modo sleep
    lora_write_reg(0x01, 0x00);
    sleep_ms(10);
    // Modo standby
    lora_write_reg(0x01, 0x81);
}

void lora_set_frequency(long frequency) {
    uint64_t frf = ((uint64_t)frequency << 19) / 32000000;
    lora_write_reg(0x06, (uint8_t)(frf >> 16));
    lora_write_reg(0x07, (uint8_t)(frf >> 8));
    lora_write_reg(0x08, (uint8_t)(frf >> 0));
}

void lora_send_packet(uint8_t *data, int length) {
    lora_write_reg(0x01, 0x81); // Standby

    lora_write_reg(0x0E, 0x00); // FIFO TX base address
    lora_write_reg(0x0D, 0x00); // FIFO pointer

    // Escreve dados no FIFO
    gpio_put(lora_cs_pin, 0);
    uint8_t reg = 0x00 | 0x80;
    spi_write_blocking(lora_spi, &reg, 1);
    spi_write_blocking(lora_spi, data, length);
    gpio_put(lora_cs_pin, 1);

    lora_write_reg(0x22, length); // Tamanho do pacote

    lora_write_reg(0x01, 0x83); // Transmit

    sleep_ms(100); // Aguarda transmissão
}

void lora_set_rx_mode(void) {
    lora_write_reg(0x01, 0x85); // Modo recepção contínua
}

int lora_receive_packet(uint8_t *buffer, int size) {
    // Verifica se pacote foi recebido (IRQ_RX_DONE_MASK = 0x40)
    if ((lora_read_reg(0x12) & 0x40) == 0)
        return 0;

    // Limpa IRQ
    lora_write_reg(0x12, 0xFF);

    // Lê tamanho do pacote
    int len = lora_read_reg(0x13);
    if (len > size) len = size;

    // Lê endereço do FIFO RX
    lora_write_reg(0x0D, lora_read_reg(0x10));

    // Lê dados do FIFO
    gpio_put(lora_cs_pin, 0);
    uint8_t reg = 0x00 & 0x7F;
    spi_write_blocking(lora_spi, &reg, 1);
    spi_read_blocking(lora_spi, 0, buffer, len);
    gpio_put(lora_cs_pin, 1);

    return len;
}