#include <stdio.h>
#include <unistd.h>
#include "user_io.h"
#include "hardware.h"
#include "hdmitx.h"
#include "it6613_drv.h"

BYTE HDMITX_ReadI2C_Byte(BYTE RegAddr)
{
    BYTE data;
    char ack = user_io_i2c_read(HDMI_TX_I2C_SLAVE_ADDR >> 1, RegAddr, &data);
    if (!ack) iprintf("HDMITX_ReadI2C_Byte RegAddr=%02x %s\n", RegAddr, ack ? "ACK" : "NAK");
    return data;
}

SYS_STATUS HDMITX_WriteI2C_Byte(BYTE RegAddr,BYTE val)
{
    return user_io_i2c_write(HDMI_TX_I2C_SLAVE_ADDR >> 1, RegAddr, val);
}

SYS_STATUS HDMITX_ReadI2C_ByteN(BYTE RegAddr,BYTE *pData,int N)
{
    int i;

    for (i=0; i<N; i++)
        pData[i] =  HDMITX_ReadI2C_Byte(RegAddr+i);

    return 0;
}

SYS_STATUS HDMITX_WriteI2C_ByteN(BYTE RegAddr,BYTE *pData,int N)
{
    int i;

    for (i=0; i<N; i++)
        HDMITX_WriteI2C_Byte(RegAddr+i, pData[i]);

    return 0;
}

void DelayMS(unsigned int ms)
{
    WaitTimer(ms);
}

char HDMITX_isdebug() {
    return user_io_dip_switch1();
}
