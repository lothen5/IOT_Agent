#ifndef __W5500_H
#define __W5500_H

#include "main.h"
#include <stdint.h>

#define W5500_VERSION_VALUE      0x04

#ifndef W5500_CS_GPIO_Port
#define W5500_CS_GPIO_Port       GPIOB
#endif

#ifndef W5500_CS_Pin
#define W5500_CS_Pin             GPIO_PIN_6
#endif

#ifndef W5500_RST_GPIO_Port
#define W5500_RST_GPIO_Port      GPIOB
#endif

#ifndef W5500_RST_Pin
#define W5500_RST_Pin            GPIO_PIN_7
#endif

void W5500_HardReset(void);
uint8_t W5500_ReadReg(uint16_t addr);
void W5500_WriteReg(uint16_t addr, uint8_t data);
uint8_t W5500_ReadVersion(void);

void W5500_NetworkConfig(void);
void W5500_PrintNetworkInfo(void);
uint8_t W5500_IsLinkUp(void);

int W5500_Socket0_ConnectTCP(const uint8_t dest_ip[4], uint16_t dest_port, uint16_t local_port);
int W5500_Socket0_Send(const uint8_t *buf, uint16_t len);
int W5500_Socket0_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);
void W5500_Socket0_Close(void);

void W5500_HTTP_GET_Test(void);

#endif