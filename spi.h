#ifndef SPI_H
#define SPI_H

/* generic helper */
void spi8(unsigned char parm);
void spi16(unsigned short parm);
void spi24(unsigned long parm);
void spi32(unsigned long parm);
void spi32le(unsigned long parm);
void spi_n(unsigned char value, unsigned short cnt);

/* OSD related SPI functions */
void spi_osd_cmd_cont(unsigned char cmd);
void spi_osd_cmd(unsigned char cmd);
void spi_osd_cmd8_cont(unsigned char cmd, unsigned char parm);
void spi_osd_cmd8(unsigned char cmd, unsigned char parm);
void spi_osd_cmd32_cont(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32le_cont(unsigned char cmd, unsigned long parm);
void spi_osd_cmd32le(unsigned char cmd, unsigned long parm);

#endif // SPI_H
