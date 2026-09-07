#include "w5500.h"

#include "spi.h"
#include "gpio.h"

#include <stdio.h>
#include <string.h>

/* Common Register */
#define W5500_MR_ADDR           0x0000
#define W5500_GAR_ADDR          0x0001
#define W5500_SUBR_ADDR         0x0005
#define W5500_SHAR_ADDR         0x0009
#define W5500_SIPR_ADDR         0x000F
#define W5500_PHYCFGR_ADDR      0x002E
#define W5500_VERSIONR_ADDR     0x0039

/* W5500 block select */
#define W5500_BLOCK_COMMON      0x00
#define W5500_BLOCK_S0_REG      0x01
#define W5500_BLOCK_S0_TX       0x02
#define W5500_BLOCK_S0_RX       0x03

#define W5500_CTRL_READ(block)  ((uint8_t)((block) << 3))
#define W5500_CTRL_WRITE(block) ((uint8_t)(((block) << 3) | 0x04))

/* Socket 0 registers */
#define Sn_MR                   0x0000
#define Sn_CR                   0x0001
#define Sn_IR                   0x0002
#define Sn_SR                   0x0003
#define Sn_PORT                 0x0004
#define Sn_DIPR                 0x000C
#define Sn_DPORT                0x0010
#define Sn_RXBUF_SIZE           0x001E
#define Sn_TXBUF_SIZE           0x001F
#define Sn_TX_FSR               0x0020
#define Sn_TX_RD                0x0022
#define Sn_TX_WR                0x0024
#define Sn_RX_RSR               0x0026
#define Sn_RX_RD                0x0028

/* Socket commands */
#define Sn_CR_OPEN              0x01
#define Sn_CR_CONNECT           0x04
#define Sn_CR_DISCON            0x08
#define Sn_CR_CLOSE             0x10
#define Sn_CR_SEND              0x20
#define Sn_CR_RECV              0x40

/* Socket status */
#define SOCK_CLOSED             0x00
#define SOCK_INIT               0x13
#define SOCK_ESTABLISHED        0x17
#define SOCK_CLOSE_WAIT         0x1C

/* Socket mode */
#define Sn_MR_TCP               0x01

/* Socket interrupt bits */
#define Sn_IR_CON               0x01
#define Sn_IR_DISCON            0x02
#define Sn_IR_RECV              0x04
#define Sn_IR_TIMEOUT           0x08
#define Sn_IR_SEND_OK           0x10

/* Socket 0 buffer size: 2KB */
#define S0_TX_RX_BUF_SIZE       2048
#define S0_BUF_MASK             0x07FF

static void W5500_CS_LOW(void)
{
    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_RESET);
}

static void W5500_CS_HIGH(void)
{
    HAL_GPIO_WritePin(W5500_CS_GPIO_Port, W5500_CS_Pin, GPIO_PIN_SET);
}

static uint8_t W5500_SPI_TxRx(uint8_t data)
{
    uint8_t rx_data = 0xFF;

    if (HAL_SPI_TransmitReceive(&hspi1, &data, &rx_data, 1, 100) != HAL_OK)
    {
        return 0xFF;
    }

    return rx_data;
}

static uint8_t W5500_ReadByte(uint16_t addr, uint8_t block)
{
    uint8_t data;

    W5500_CS_LOW();

    W5500_SPI_TxRx((uint8_t)(addr >> 8));
    W5500_SPI_TxRx((uint8_t)(addr & 0xFF));
    W5500_SPI_TxRx(W5500_CTRL_READ(block));

    data = W5500_SPI_TxRx(0xFF);

    W5500_CS_HIGH();

    return data;
}

static void W5500_WriteByte(uint16_t addr, uint8_t block, uint8_t data)
{
    W5500_CS_LOW();

    W5500_SPI_TxRx((uint8_t)(addr >> 8));
    W5500_SPI_TxRx((uint8_t)(addr & 0xFF));
    W5500_SPI_TxRx(W5500_CTRL_WRITE(block));

    W5500_SPI_TxRx(data);

    W5500_CS_HIGH();
}

static void W5500_ReadData(uint16_t addr, uint8_t block, uint8_t *buf, uint16_t len)
{
    W5500_CS_LOW();

    W5500_SPI_TxRx((uint8_t)(addr >> 8));
    W5500_SPI_TxRx((uint8_t)(addr & 0xFF));
    W5500_SPI_TxRx(W5500_CTRL_READ(block));

    for (uint16_t i = 0; i < len; i++)
    {
        buf[i] = W5500_SPI_TxRx(0xFF);
    }

    W5500_CS_HIGH();
}

static void W5500_WriteData(uint16_t addr, uint8_t block, const uint8_t *buf, uint16_t len)
{
    W5500_CS_LOW();

    W5500_SPI_TxRx((uint8_t)(addr >> 8));
    W5500_SPI_TxRx((uint8_t)(addr & 0xFF));
    W5500_SPI_TxRx(W5500_CTRL_WRITE(block));

    for (uint16_t i = 0; i < len; i++)
    {
        W5500_SPI_TxRx(buf[i]);
    }

    W5500_CS_HIGH();
}

static uint16_t W5500_Read16(uint16_t addr, uint8_t block)
{
    uint16_t value;

    value = ((uint16_t)W5500_ReadByte(addr, block)) << 8;
    value |= W5500_ReadByte(addr + 1, block);

    return value;
}

static void W5500_Write16(uint16_t addr, uint8_t block, uint16_t value)
{
    W5500_WriteByte(addr, block, (uint8_t)(value >> 8));
    W5500_WriteByte(addr + 1, block, (uint8_t)(value & 0xFF));
}

static void W5500_Socket0_Command(uint8_t cmd)
{
    W5500_WriteByte(Sn_CR, W5500_BLOCK_S0_REG, cmd);

    while (W5500_ReadByte(Sn_CR, W5500_BLOCK_S0_REG) != 0)
    {
    }
}

uint8_t W5500_ReadReg(uint16_t addr)
{
    return W5500_ReadByte(addr, W5500_BLOCK_COMMON);
}

void W5500_WriteReg(uint16_t addr, uint8_t data)
{
    W5500_WriteByte(addr, W5500_BLOCK_COMMON, data);
}

void W5500_HardReset(void)
{
    W5500_CS_HIGH();

    HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);

    HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(500);
}

uint8_t W5500_ReadVersion(void)
{
    return W5500_ReadReg(W5500_VERSIONR_ADDR);
}

void W5500_NetworkConfig(void)
{
    uint8_t gateway[4] = {192, 168, 1, 1};
    uint8_t subnet[4]  = {255, 255, 255, 0};
    uint8_t mac[6]     = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33};
    uint8_t ip[4]      = {192, 168, 1, 123};

    W5500_WriteData(W5500_GAR_ADDR, W5500_BLOCK_COMMON, gateway, 4);
    W5500_WriteData(W5500_SUBR_ADDR, W5500_BLOCK_COMMON, subnet, 4);
    W5500_WriteData(W5500_SHAR_ADDR, W5500_BLOCK_COMMON, mac, 6);
    W5500_WriteData(W5500_SIPR_ADDR, W5500_BLOCK_COMMON, ip, 4);

    /* Socket0 TX/RX buffer each 2KB */
    W5500_WriteByte(Sn_RXBUF_SIZE, W5500_BLOCK_S0_REG, 2);
    W5500_WriteByte(Sn_TXBUF_SIZE, W5500_BLOCK_S0_REG, 2);
}

void W5500_PrintNetworkInfo(void)
{
    uint8_t gateway[4];
    uint8_t subnet[4];
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t phy;

    W5500_ReadData(W5500_GAR_ADDR, W5500_BLOCK_COMMON, gateway, 4);
    W5500_ReadData(W5500_SUBR_ADDR, W5500_BLOCK_COMMON, subnet, 4);
    W5500_ReadData(W5500_SHAR_ADDR, W5500_BLOCK_COMMON, mac, 6);
    W5500_ReadData(W5500_SIPR_ADDR, W5500_BLOCK_COMMON, ip, 4);

    phy = W5500_ReadReg(W5500_PHYCFGR_ADDR);

    printf("W5500 Network Info:\r\n");
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    printf("IP: %d.%d.%d.%d\r\n", ip[0], ip[1], ip[2], ip[3]);
    printf("Gateway: %d.%d.%d.%d\r\n", gateway[0], gateway[1], gateway[2], gateway[3]);
    printf("Subnet: %d.%d.%d.%d\r\n", subnet[0], subnet[1], subnet[2], subnet[3]);
    printf("PHYCFGR = 0x%02X\r\n", phy);

    if (phy & 0x01)
    {
        printf("Ethernet Link: UP\r\n");
    }
    else
    {
        printf("Ethernet Link: DOWN\r\n");
    }
}

uint8_t W5500_IsLinkUp(void)
{
    uint8_t phy = W5500_ReadReg(W5500_PHYCFGR_ADDR);

    return (phy & 0x01) ? 1 : 0;
}

void W5500_Socket0_Close(void)
{
    W5500_Socket0_Command(Sn_CR_CLOSE);
    W5500_WriteByte(Sn_IR, W5500_BLOCK_S0_REG, 0xFF);
}

int W5500_Socket0_ConnectTCP(const uint8_t dest_ip[4], uint16_t dest_port, uint16_t local_port)
{
    uint32_t tickstart;

    W5500_Socket0_Close();

    W5500_WriteByte(Sn_MR, W5500_BLOCK_S0_REG, Sn_MR_TCP);
    W5500_Write16(Sn_PORT, W5500_BLOCK_S0_REG, local_port);

    W5500_Socket0_Command(Sn_CR_OPEN);

    tickstart = HAL_GetTick();
    while (W5500_ReadByte(Sn_SR, W5500_BLOCK_S0_REG) != SOCK_INIT)
    {
        if ((HAL_GetTick() - tickstart) > 1000)
        {
            printf("Socket OPEN timeout\r\n");
            return -1;
        }
    }

    W5500_WriteData(Sn_DIPR, W5500_BLOCK_S0_REG, dest_ip, 4);
    W5500_Write16(Sn_DPORT, W5500_BLOCK_S0_REG, dest_port);

    W5500_Socket0_Command(Sn_CR_CONNECT);

    tickstart = HAL_GetTick();
    while (1)
    {
        uint8_t sr = W5500_ReadByte(Sn_SR, W5500_BLOCK_S0_REG);

        if (sr == SOCK_ESTABLISHED)
        {
            printf("TCP connected\r\n");
            return 0;
        }

        if (sr == SOCK_CLOSED)
        {
            printf("TCP closed during connect\r\n");
            return -2;
        }

        if ((HAL_GetTick() - tickstart) > 5000)
        {
            printf("TCP connect timeout, SR=0x%02X\r\n", sr);
            W5500_Socket0_Close();
            return -3;
        }
    }
}

static void W5500_Socket0_WriteTxBuffer(uint16_t ptr, const uint8_t *buf, uint16_t len)
{
    uint16_t offset = ptr & S0_BUF_MASK;
    uint16_t first_len = len;

    if ((offset + len) > S0_TX_RX_BUF_SIZE)
    {
        first_len = S0_TX_RX_BUF_SIZE - offset;
    }

    W5500_WriteData(offset, W5500_BLOCK_S0_TX, buf, first_len);

    if (len > first_len)
    {
        W5500_WriteData(0, W5500_BLOCK_S0_TX, buf + first_len, len - first_len);
    }
}

static void W5500_Socket0_ReadRxBuffer(uint16_t ptr, uint8_t *buf, uint16_t len)
{
    uint16_t offset = ptr & S0_BUF_MASK;
    uint16_t first_len = len;

    if ((offset + len) > S0_TX_RX_BUF_SIZE)
    {
        first_len = S0_TX_RX_BUF_SIZE - offset;
    }

    W5500_ReadData(offset, W5500_BLOCK_S0_RX, buf, first_len);

    if (len > first_len)
    {
        W5500_ReadData(0, W5500_BLOCK_S0_RX, buf + first_len, len - first_len);
    }
}

int W5500_Socket0_Send(const uint8_t *buf, uint16_t len)
{
    uint16_t tx_fsr;
    uint16_t tx_wr;
    uint32_t tickstart;

    if (len == 0 || len > S0_TX_RX_BUF_SIZE)
    {
        return -1;
    }

    tickstart = HAL_GetTick();
    do
    {
        tx_fsr = W5500_Read16(Sn_TX_FSR, W5500_BLOCK_S0_REG);

        if ((HAL_GetTick() - tickstart) > 3000)
        {
            printf("TX buffer wait timeout\r\n");
            return -2;
        }
    } while (tx_fsr < len);

    tx_wr = W5500_Read16(Sn_TX_WR, W5500_BLOCK_S0_REG);

    W5500_Socket0_WriteTxBuffer(tx_wr, buf, len);

    W5500_Write16(Sn_TX_WR, W5500_BLOCK_S0_REG, tx_wr + len);

    W5500_Socket0_Command(Sn_CR_SEND);

    tickstart = HAL_GetTick();
    while (1)
    {
        uint8_t ir = W5500_ReadByte(Sn_IR, W5500_BLOCK_S0_REG);

        if (ir & Sn_IR_SEND_OK)
        {
            W5500_WriteByte(Sn_IR, W5500_BLOCK_S0_REG, Sn_IR_SEND_OK);
            return len;
        }

        if (ir & Sn_IR_TIMEOUT)
        {
            W5500_WriteByte(Sn_IR, W5500_BLOCK_S0_REG, Sn_IR_TIMEOUT);
            printf("SEND timeout interrupt\r\n");
            return -3;
        }

        if ((HAL_GetTick() - tickstart) > 3000)
        {
            printf("SEND wait timeout\r\n");
            return -4;
        }
    }
}

int W5500_Socket0_Recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint16_t rx_size;
    uint16_t rx_rd;
    uint16_t read_len;
    uint32_t tickstart = HAL_GetTick();

    while (1)
    {
        rx_size = W5500_Read16(Sn_RX_RSR, W5500_BLOCK_S0_REG);

        if (rx_size > 0)
        {
            break;
        }

        if ((HAL_GetTick() - tickstart) > timeout_ms)
        {
            return 0;
        }

        if (W5500_ReadByte(Sn_SR, W5500_BLOCK_S0_REG) == SOCK_CLOSED)
        {
            return -1;
        }
    }

    read_len = rx_size;

    if (read_len > max_len)
    {
        read_len = max_len;
    }

    rx_rd = W5500_Read16(Sn_RX_RD, W5500_BLOCK_S0_REG);

    W5500_Socket0_ReadRxBuffer(rx_rd, buf, read_len);

    W5500_Write16(Sn_RX_RD, W5500_BLOCK_S0_REG, rx_rd + read_len);

    W5500_Socket0_Command(Sn_CR_RECV);

    return read_len;
}

void W5500_HTTP_GET_Test(void)
{
    uint8_t server_ip[4] = {192, 168, 1, 100};
    char request[] =
        "GET / HTTP/1.1\r\n"
        "Host: 192.168.1.100\r\n"
        "Connection: close\r\n"
        "\r\n";

    uint8_t rx_buf[512];
    int ret;

    printf("HTTP GET test start\r\n");

    ret = W5500_Socket0_ConnectTCP(server_ip, 8080, 50000);

    if (ret != 0)
    {
        printf("TCP connect failed: %d\r\n", ret);
        return;
    }

    ret = W5500_Socket0_Send((const uint8_t *)request, (uint16_t)strlen(request));

    if (ret < 0)
    {
        printf("HTTP request send failed: %d\r\n", ret);
        W5500_Socket0_Close();
        return;
    }

    printf("HTTP request sent, len=%d\r\n", ret);

    while (1)
    {
        ret = W5500_Socket0_Recv(rx_buf, sizeof(rx_buf) - 1, 3000);

        if (ret > 0)
        {
            rx_buf[ret] = '\0';
            printf("%s", rx_buf);
        }
        else
        {
            break;
        }
    }

    printf("\r\nHTTP GET test done\r\n");

    W5500_Socket0_Close();
}
