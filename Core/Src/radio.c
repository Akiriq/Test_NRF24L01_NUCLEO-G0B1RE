/*
 * radio.c
 *
 *  Created on: February 21, 2025
 *      Author: rguilletlhomat
 */
#include <memory.h>
#include "support.h"
#include "nrf24.h"
#include "ssd1306.h"
#include "fonts.h"

#define HEX_CHARS      "0123456789ABCDEF"

extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c1;
extern void Error_Handler(void);


void UART_SendChar(char b) {
    HAL_UART_Transmit(&huart2, (uint8_t *) &b, 1, 200);
}

void UART_SendStr(char *string) {
    HAL_UART_Transmit(&huart2, (uint8_t *) string, (uint16_t) strlen(string), 200);
}

void Toggle_LED() {
    HAL_GPIO_TogglePin(LD2_GPIO_Port,LD2_Pin);
}

void UART_SendBufHex(char *buf, uint16_t bufsize) {
    uint16_t i;
    char ch;
    for (i = 0; i < bufsize; i++) {
        ch = *buf++;
        UART_SendChar(HEX_CHARS[(ch >> 4) % 0x10]);
        UART_SendChar(HEX_CHARS[(ch & 0x0f) % 0x10]);
    }
}

void UART_SendHex8(uint16_t num) {
    UART_SendChar(HEX_CHARS[(num >> 4) % 0x10]);
    UART_SendChar(HEX_CHARS[(num & 0x0f) % 0x10]);
}

void UART_SendInt(int32_t num) {
    char str[10]; // 10 chars max for INT32_MAX
    int i = 0;
    if (num < 0) {
        UART_SendChar('-');
        num *= -1;
    }
    do str[i++] = (char) (num % 10 + '0'); while ((num /= 10) > 0);
    for (i--; i >= 0; i--) UART_SendChar(str[i]);
}

void SCREEN_Print_BufHex(uint8_t *buf, uint16_t bufsize,nRF24_RXResult pipe){
	char str[bufsize];

	for (uint16_t i = 0; i < bufsize; i++){
		char ch = (char)buf[i];
		str[2*i+0] = HEX_CHARS[(ch >> 4) % 0x10];
		str[2*i+1] = HEX_CHARS[(ch & 0x0f) % 0x10];
	}
	char pipeStr[2];
	pipeStr[0] = HEX_CHARS[(pipe >> 4) % 0x10];
	pipeStr[1] = HEX_CHARS[(pipe & 0x0f) % 0x10];
	ssd1306_Fill(Black);
	ssd1306_SetCursor(0, 0);
	ssd1306_WriteString("RCV PIPE#", Font_11x18, White);
	ssd1306_WriteString((const char*)pipeStr, Font_11x18, White);
    ssd1306_SetCursor(0, 20);
    ssd1306_WriteString((const char*)str, Font_11x18, White);
    ssd1306_UpdateScreen(&hi2c1);
}

void SCREEN_Print_L1(const char* string) {
	ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(string, Font_11x18, White);
    ssd1306_UpdateScreen(&hi2c1);
}

void SCREEN_Print_L1_l2(const char* string1,const char* string2) {
	ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(string1, Font_11x18, White);
    ssd1306_SetCursor(0, 20);
    ssd1306_WriteString(string2, Font_11x18, White);
    ssd1306_UpdateScreen(&hi2c1);
}

uint8_t nRF24_payload[32];

// Pipe number
nRF24_RXResult pipe;

uint32_t i, j, k;

// Length of received pay_load
uint8_t payload_length;

#define DEMO_RX_SINGLE      1 // <-	Single address receiver (1 pipe)
#define DEMO_RX_MULTI       0 // 	Multiple address receiver (3 pipes)
#define DEMO_RX_SOLAR       0 // 	Solar temperature sensor receiver
#define DEMO_TX_SINGLE      0 // <-	Single address transmitter (1 pipe)
#define DEMO_TX_MULTI       0 // 	Multiple address transmitter (3 pipes)
#define DEMO_RX_SINGLE_ESB  0 // 	Single address receiver with Enhanced ShockBurst (1 pipe)
#define DEMO_TX_SINGLE_ESB  0 // 	Single address transmitter with Enhanced ShockBurst (1 pipe)
#define DEMO_RX_ESB_ACK_PL  0 // 	Single address receiver with Enhanced ShockBurst (1 pipe) + payload sent back
#define DEMO_TX_ESB_ACK_PL  0 // 	Single address transmitter with Enhanced ShockBurst (1 pipe) + payload received in ACK

// Kinda foolproof :)
#if ((DEMO_RX_SINGLE + DEMO_RX_MULTI + DEMO_RX_SOLAR + DEMO_TX_SINGLE + DEMO_TX_MULTI + DEMO_RX_SINGLE_ESB + DEMO_TX_SINGLE_ESB + DEMO_RX_ESB_ACK_PL + DEMO_TX_ESB_ACK_PL) != 1)
#error "Define only one DEMO_xx, use the '1' value"
#endif


#if ((DEMO_TX_SINGLE) || (DEMO_TX_MULTI) || (DEMO_TX_SINGLE_ESB) || (DEMO_TX_ESB_ACK_PL) )

// Helpers for transmit mode demo

// Timeout counter (depends on the CPU speed)
// Used for not stuck waiting for IRQ
#define nRF24_WAIT_TIMEOUT         (uint32_t)0x000FFFFF

// Result of packet transmission
typedef enum {
	nRF24_TX_ERROR  = (uint8_t)0x00, // Unknown error
	nRF24_TX_SUCCESS,                // Packet has been transmitted successfully
	nRF24_TX_TIMEOUT,                // It was timeout during packet transmit
	nRF24_TX_MAXRT                   // Transmit failed with maximum auto retransmit count
} nRF24_TXResult;

nRF24_TXResult tx_res;

// Function to transmit data packet
// input:
//   pBuf - pointer to the buffer with data to transmit
//   length - length of the data buffer in bytes
// return: one of nRF24_TX_xx values
nRF24_TXResult nRF24_TransmitPacket(uint8_t *pBuf, uint8_t length) {
	volatile uint32_t wait = nRF24_WAIT_TIMEOUT;
	uint8_t status;

	// Deassert the CE pin (in case if it still high)
	nRF24_CE_L();

	// Transfer a data from the specified buffer to the TX FIFO
	nRF24_WritePayload(pBuf, length);

	// Start a transmission by asserting CE pin (must be held at least 10us)
	nRF24_CE_H();

	// Poll the transceiver status register until one of the following flags will be set:
	//   TX_DS  - means the packet has been transmitted
	//   MAX_RT - means the maximum number of TX retransmits happened
	// note: this solution is far from perfect, better to use IRQ instead of polling the status
	do {
		status = nRF24_GetStatus();
		if (status & (nRF24_FLAG_TX_DS | nRF24_FLAG_MAX_RT)) {
			break;
		}
	} while (wait--);

	// Deassert the CE pin (Standby-II --> Standby-I)
	nRF24_CE_L();

	if (!wait) {
		// Timeout
		return nRF24_TX_TIMEOUT;
	}

	// Check the flags in STATUS register
	UART_SendStr("[");
	UART_SendHex8(status);
	UART_SendStr("] ");

	// Clear pending IRQ flags
    nRF24_ClearIRQFlags();

	if (status & nRF24_FLAG_MAX_RT) {
		// Auto retransmit counter exceeds the programmed maximum limit (FIFO is not removed)
		return nRF24_TX_MAXRT;
	}

	if (status & nRF24_FLAG_TX_DS) {
		// Successful transmission
		return nRF24_TX_SUCCESS;
	}

	// Some banana happens, a payload remains in the TX FIFO, flush it
	nRF24_FlushTX();

	return nRF24_TX_ERROR;
}

#endif // DEMO_TX_



void runRadio(void)
{
	// start the screen
	if (ssd1306_Init(&hi2c1) != 0) {Error_Handler();}

	UART_SendStr("\r\nNUCLEO-G0B1RE is online.\r\n");
	SCREEN_Print_L1_l2("NUCLEO  ", "ready   ");
	HAL_Delay(1000);
	// RX/TX disabled
	nRF24_CE_L();

	// Configure the nRF24L01+
	UART_SendStr("nRF24L01+ check: ");
	SCREEN_Print_L1_l2("NRF24L01+", "check  ");
	HAL_Delay(1000);
	if (!nRF24_Check()) {
		UART_SendStr("FAIL\r\n");
		SCREEN_Print_L1_l2("NRF24L01+", "FAIL   ");
		while (1) {
			Toggle_LED();
			Delay_ms(100);
		}
	}

	UART_SendStr("OK\r\n");
	SCREEN_Print_L1_l2("NRF24L01+", "online  ");

	// Initialize the nRF24L01 to its default state
	nRF24_Init();


#if (DEMO_RX_SINGLE)

	// This is simple receiver with one RX pipe:
	//   - pipe#1 address: '0xE7 0x1C 0xE3'
	//   - payload: 5 bytes
	//   - RF channel: 115 (2515MHz)
	//   - data rate: 250kbps (minimum possible, to increase reception reliability)
	//   - CRC scheme: 2 byte

    // The transmitter sends a 5-byte packets to the address '0xE7 0x1C 0xE3' without Auto-ACK (ShockBurst disabled)

    // Disable ShockBurst for all RX pipes
    nRF24_DisableAA(0xFF);

    // Set RF channel
    nRF24_SetRFChannel(115);

    // Set data rate
    nRF24_SetDataRate(nRF24_DR_250kbps);

    // Set CRC scheme
    nRF24_SetCRCScheme(nRF24_CRC_2byte);

    // Set address width, its common for all pipes (RX and TX)
    nRF24_SetAddrWidth(3);

    // Configure RX PIPE#1
    static const uint8_t nRF24_ADDR[] = { 0xE7, 0x1C, 0xE3 };
    nRF24_SetAddr(nRF24_PIPE1, nRF24_ADDR); // program address for RX pipe #1
    nRF24_SetRXPipe(nRF24_PIPE1, nRF24_AA_OFF, 5); // Auto-ACK: disabled, payload length: 5 bytes

    // Set operational mode (PRX == receiver)
    nRF24_SetOperationalMode(nRF24_MODE_RX);

    // Wake the transceiver
    nRF24_SetPowerMode(nRF24_PWR_UP);

    // Put the transceiver to the RX mode
    nRF24_CE_H();


    // The main loop
    while (1) {
    	//
    	// Constantly poll the status of the RX FIFO and get a payload if FIFO is not empty
    	//
    	// This is far from best solution, but it's ok for testing purposes
    	// More smart way is to use the IRQ pin :)
    	//
    	if (nRF24_GetStatus_RXFIFO() != nRF24_STATUS_RXFIFO_EMPTY) {
    		// Get a payload from the transceiver
    		pipe = nRF24_ReadPayload(nRF24_payload, &payload_length);

    		// Clear all pending IRQ flags
			nRF24_ClearIRQFlags();

			// Print a payload contents to UART
			UART_SendStr("RCV PIPE#");
			UART_SendInt(pipe);
			UART_SendStr(" PAYLOAD:>");
			UART_SendBufHex((char *)nRF24_payload, payload_length);
			UART_SendStr("<\r\n");
			SCREEN_Print_BufHex(nRF24_payload, payload_length, pipe);
    	}
    }

#endif // DEMO_RX_SINGLE


#if (DEMO_TX_SINGLE)

	// This is simple transmitter (to one logic address):
	//   - TX address: '0xE7 0x1C 0xE3'
	//   - payload: 5 bytes
	//   - RF channel: 115 (2515MHz)
	//   - data rate: 250kbps (minimum possible, to increase reception reliability)
	//   - CRC scheme: 2 byte

    // The transmitter sends a 5-byte packets to the address '0xE7 0x1C 0xE3' without Auto-ACK (ShockBurst disabled)

    // Disable ShockBurst for all RX pipes
    nRF24_DisableAA(0xFF);

    // Set RF channel
    nRF24_SetRFChannel(115);

    // Set data rate
    nRF24_SetDataRate(nRF24_DR_250kbps);

    // Set CRC scheme
    nRF24_SetCRCScheme(nRF24_CRC_2byte);

    // Set address width, its common for all pipes (RX and TX)
    nRF24_SetAddrWidth(3);

    // Configure TX PIPE
    static const uint8_t nRF24_ADDR[] = { 0xE7, 0x1C, 0xE3 };
    nRF24_SetAddr(nRF24_PIPETX, nRF24_ADDR); // program TX address

    // Set TX power (maximum)
    nRF24_SetTXPower(nRF24_TXPWR_0dBm);

    // Set operational mode (PTX == transmitter)
    nRF24_SetOperationalMode(nRF24_MODE_TX);

    // Clear any pending IRQ flags
    nRF24_ClearIRQFlags();

    // Wake the transceiver
    nRF24_SetPowerMode(nRF24_PWR_UP);


    // The main loop
    j = 0;
    payload_length = 5;
    while (1) {
    	// Prepare data packet
    	for (i = 0; i < payload_length; i++) {
    		nRF24_payload[i] = j++;
    		if (j > 0x000000FF) j = 0;
    	}

    	// Print a payload
    	UART_SendStr("PAYLOAD:>");
    	UART_SendBufHex((char *)nRF24_payload, payload_length);
    	UART_SendStr("< ... TX: ");

    	// Transmit a packet
    	tx_res = nRF24_TransmitPacket(nRF24_payload, payload_length);
    	switch (tx_res) {
			case nRF24_TX_SUCCESS:
				UART_SendStr("OK");
				break;
			case nRF24_TX_TIMEOUT:
				UART_SendStr("TIMEOUT");
				break;
			case nRF24_TX_MAXRT:
				UART_SendStr("MAX RETRANSMIT");
				break;
			default:
				UART_SendStr("ERROR");
				break;
		}
    	UART_SendStr("\r\n");

    	// Wait ~0.5s
    	Delay_ms(500);
    }

#endif // DEMO_TX_SINGLE
}
