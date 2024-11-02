/*
 * data_io.h
 * Data transfer functions using SPI_SS2
 *
 */

#ifndef DATA_IO_H
#define DATA_IO_H

#include <inttypes.h>

#define DIO_FILE_TX     0x53
#define DIO_FILE_TX_DAT 0x54
#define DIO_FILE_INDEX  0x55
#define DIO_FILE_INFO   0x56
#define DIO_FILE_RX     0x57
#define DIO_FILE_RX_DAT 0x58

void data_io_file_tx_prepare(FIL *file, char index, const char *ext);
void data_io_set_index(char index);
void data_io_file_tx_start();
void data_io_file_tx_done();
void data_io_fill_tx(unsigned char, unsigned int, char);
void data_io_file_tx(FIL*, char, const char*);
void data_io_file_rx(FIL*, char, unsigned int);

// called when a rom entry is found in the mist.ini
void data_io_rom_upload(char *s, char mode);

#endif // DATA_IO_H
