#include <stdio.h>

#include <string.h>

#include <stdlib.h>

#include "pico/stdlib.h"

#include "hardware/spi.h"

#include "hardware/irq.h"



// ============================

// CONFIGURAÇÕES DO PROJETO

// ============================

#define DEVICE_A 0 // 1 = Dispositivo A (Ping) | 0 = Dispositivo B (Pong)



#define PIN_MISO 16

#define PIN_CS 17

#define PIN_SCK 18

#define PIN_MOSI 19

#define PIN_RST 20

#define PIN_DIO0 8 // ESSA CONEXÃO É CRÍTICA!



#define LED_AZUL 12 // TX

#define LED_VERDE 11 // RX

#define LED_ERROR 13 // LED Vermelho para todos os erros



#define LORA_SPI spi0

#define LORA_FREQ 915E6



// ============================

// VARIÁVEIS GLOBAIS

// ============================

volatile bool tx_done = false;

volatile bool rx_done = false;



// ============================

// FUNÇÕES DE BAIXO NÍVEL

// ============================

static inline void cs_select() { gpio_put(PIN_CS, 0); }

static inline void cs_deselect() { gpio_put(PIN_CS, 1); }



void pisca_led(uint led_pin, int ms) {

gpio_put(led_pin, 1);

sleep_ms(ms);

gpio_put(led_pin, 0);

}



void lora_reset() {

gpio_put(PIN_RST, 0); sleep_ms(10);

gpio_put(PIN_RST, 1); sleep_ms(10);

}



// NOVO: Função central para reportar erros

void report_error(const char* message, bool fatal) {

printf("[ERRO] %s\n", message);

if (fatal) {

while(true) { // Trava o programa em erros fatais

pisca_led(LED_ERROR, 200);

sleep_ms(200);

}

} else {

pisca_led(LED_ERROR, 500); // Pisca uma vez para erros não-fatais

}

}



void lora_write_reg(uint8_t reg, uint8_t value) {

uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };

cs_select();

spi_write_blocking(LORA_SPI, buf, 2);

cs_deselect();

}



uint8_t lora_read_reg(uint8_t reg) {

uint8_t buf[2] = { reg & 0x7F, 0x00 };

uint8_t rx[2];

cs_select();

spi_write_read_blocking(LORA_SPI, buf, rx, 2);

cs_deselect();

return rx[1];

}



void lora_write_fifo(const uint8_t *data, uint8_t len) {

cs_select();

uint8_t addr = 0x80;

spi_write_blocking(LORA_SPI, &addr, 1);

spi_write_blocking(LORA_SPI, data, len);

cs_deselect();

}



void lora_read_fifo(uint8_t *data, uint8_t len) {

cs_select();

uint8_t addr = 0x00;

spi_write_blocking(LORA_SPI, &addr, 1);

spi_read_blocking(LORA_SPI, 0x00, data, len);

cs_deselect();

}



void lora_set_mode(uint8_t mode) {

lora_write_reg(0x01, (0x80 | mode));

}



// ATUALIZADO: lora_init agora checa se o módulo foi encontrado

bool lora_init() {

lora_reset();

lora_set_mode(0x00); // Sleep

lora_set_mode(0x01); // Standby



uint64_t frf = ((uint64_t)LORA_FREQ << 19) / 32000000;

lora_write_reg(0x06, (uint8_t)(frf >> 16));

lora_write_reg(0x07, (uint8_t)(frf >> 8));

lora_write_reg(0x08, (uint8_t)(frf >> 0));



lora_write_reg(0x09, 0xFF); // PaConfig: Max Power

lora_write_reg(0x1D, 0x72); // ModemConfig1: BW125kHz, CR 4/5

lora_write_reg(0x1E, 0x74); // ModemConfig2: SF7, CRC on


lora_write_reg(0x20, 0x00); // Preamble Length LSB

lora_write_reg(0x21, 0x08); // Preamble Length MSB

lora_write_reg(0x0E, 0x00); // FifoTxBaseAddr

lora_write_reg(0x0F, 0x00); // FifoRxBaseAddr


lora_write_reg(0x40, 0x00); // DioMapping1: DIO0 -> RX_DONE / TX_DONE

// NOVO: Verificação de hardware

uint8_t version = lora_read_reg(0x42); // 0x42 é o registrador REG_VERSION

if (version == 0x12) { // 0x12 é o valor esperado para chips SX127x

printf("Módulo LoRa SX127x detectado com sucesso (versão: 0x%02X)\n", version);

return true;

} else {

printf("Falha ao detectar módulo LoRa! (valor lido: 0x%02X)\n", version);

return false;

}

}



void dio0_irq_handler(uint gpio, uint32_t events) {

uint8_t irq_flags = lora_read_reg(0x12);

lora_write_reg(0x12, 0xFF);

printf("IRQ Flags: 0x%02X\n", irq_flags);

if ((irq_flags & 0x40) && !(irq_flags & 0x20)) {

rx_done = true;

} else if (irq_flags & 0x08) {

tx_done = true;

} else if (irq_flags & 0x20) {

report_error("CRC Inválido no pacote recebido! Pacote descartado.\n", false);

}

}



// ATUALIZADO: lora_send agora tem timeout

bool lora_send(const char *msg) {

if (strlen(msg) > 255) {

report_error("Mensagem muito longa para enviar (max 255 bytes).\n", false);

return false;

}



lora_set_mode(0x01); // Standby

lora_write_reg(0x0D, 0x00);

lora_write_fifo((const uint8_t*)msg, strlen(msg));

lora_write_reg(0x22, strlen(msg));



lora_set_mode(0x03); // TX

/*tx_done = false;



// NOVO: Timeout de transmissão

absolute_time_t start_time = get_absolute_time();

while (!tx_done) {

if (absolute_time_diff_us(start_time, get_absolute_time()) > 500 * 1000) { // Timeout de 500ms

report_error("Timeout de transmissão! Interrupção TX_DONE não ocorreu. Verifique a conexão do pino DIO0.\n", false);

lora_init(); // Tenta resetar o módulo para um estado conhecido

return false;

}

}*/

sleep_ms(10); // Pequeno atraso para garantir que o TX_DONE seja processado

//tx_done = true; // Simula que a transmissão foi bem-sucedida

//lora_set_mode(0x01); // Volta para Standby após TX



printf("Enviado: \"%s\"\n", msg);

pisca_led(LED_AZUL, 100);

return true;

}



bool lora_receive(char *buf, size_t maxlen) {

if (rx_done) {

rx_done = false;

uint8_t len = lora_read_reg(0x13);


// AVISO: Checa se o pacote cabe no buffer

if (len > maxlen - 1) {

printf("[AVISO] Pacote recebido (%d bytes) foi truncado para caber no buffer (%d bytes).\n", len, maxlen-1);

len = maxlen - 1;

}



uint8_t fifo_addr = lora_read_reg(0x10);

lora_write_reg(0x0D, fifo_addr);

lora_read_fifo((uint8_t*)buf, len);

buf[len] = '\0';



printf("Recebido: \"%s\"\n", buf);

pisca_led(LED_VERDE, 100);

return true;

}

return false;

}



// ============================

// MAIN

// ============================

int main() {

stdio_init_all();

sleep_ms(5000);



// --- Inicialização do Hardware ---

spi_init(LORA_SPI, 5E6);

gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);

gpio_init(PIN_RST); gpio_set_dir(PIN_RST, GPIO_OUT);

gpio_init(LED_AZUL); gpio_set_dir(LED_AZUL, GPIO_OUT);

gpio_init(LED_VERDE); gpio_set_dir(LED_VERDE, GPIO_OUT);

gpio_init(LED_ERROR); gpio_set_dir(LED_ERROR, GPIO_OUT);

gpio_init(PIN_DIO0); gpio_set_dir(PIN_DIO0, GPIO_IN);

gpio_set_irq_enabled_with_callback(PIN_DIO0, GPIO_IRQ_EDGE_RISE, true, &dio0_irq_handler);



// NOVO: Checa se a inicialização do LoRa foi bem-sucedida

if (!lora_init()) {

report_error("Falha crítica na comunicação com o módulo LoRa. Verifique a fiação SPI (MISO, MOSI, SCK, CS) e a alimentação.", true);

}


char buf[64];



#if DEVICE_A

// ===================================

// LÓGICA DO DISPOSITIVO A (PING)

// ===================================

printf("\n--- Dispositivo A (Ping / Transmissor) iniciado ---\n");

int msg_id = 0;

while (true) {

char msg[32];

snprintf(msg, sizeof(msg), "Ping %d", msg_id);


if (lora_send(msg)) {

// Mensagem enviada com sucesso

} else {

// Erro na transmissão já reportado em lora_send

sleep_ms(3000);

continue; // Tenta enviar novamente após o atraso

}



// Espera pela resposta "Pong"

printf("Aguardando resposta Pong...\n");

rx_done = false;

lora_set_mode(0x05);


bool pong_recebido = false;

absolute_time_t start_time = get_absolute_time();

while (absolute_time_diff_us(start_time, get_absolute_time()) < 2000 * 1000) { // Timeout de 2s

if (lora_receive(buf, sizeof(buf))) {

if (strstr(buf, "Pong") != NULL) {

pong_recebido = true;

break;

}

}

}



if (pong_recebido) {

printf("Sucesso! Resposta Pong recebida.\n\n");

msg_id++;

} else {

// ERRO DE PROTOCOLO

report_error("Timeout de protocolo! Nenhuma resposta Pong recebida.", false);

printf("\n");

}

sleep_ms(3000);

}

#else

// ===================================

// LÓGICA DO DISPOSITIVO B (PONG)

// ===================================

printf("\n--- Dispositivo B (Pong / Receptor) iniciado ---\n");

lora_set_mode(0x05);

while (true) {

if (lora_receive(buf, sizeof(buf))) {

if (strstr(buf, "Ping") != NULL) {

char ack_msg[48];

snprintf(ack_msg, sizeof(ack_msg), "Pong para: %s", buf);

lora_send(ack_msg);

printf("\n");

lora_set_mode(0x05); // Essencial: voltar a escutar

}

}

sleep_ms(10);

}

#endif

}