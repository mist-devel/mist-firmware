#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user_io.h"
#include "data_io.h"
#include "debug.h"
#include "spi.h"

extern unsigned long iCurrentDirectory;    // cluster number of current directory, 0 for root


extern fileTYPE file;

extern DIRENTRY DirEntry[MAXDIRENTRIES];
extern unsigned char iSelectedEntry;
extern unsigned char sort_table[MAXDIRENTRIES];

// core supports direct ROM upload via SS4
char rom_direct_upload = 0;


void data_io_set_index(unsigned char index) {
  EnableFpga();
  SPI(DIO_FILE_INDEX);
  SPI(index);
  DisableFpga();
}

///////////////////////////
// TRANSMIT FILE TO FPGA //
///////////////////////////

static void data_io_file_tx_prepare(fileTYPE *file, unsigned char index) {
  iprintf("Preparing transmission for index %d\n", index);

  // set index byte (0=bios rom, 1-n=OSD entry index)
  data_io_set_index(index);

  // send directory entry
  EnableFpga();
  SPI(DIO_FILE_INFO);
  if (file && !index) {
    // Synthesize a directory entry for index=0 (ROM)
    spi_write((void*)file, 11); // name+ext
    spi_write((void*)&file->attributes, 1);
    spi_n(0, 16);
    spi_write((void*)&file->size, 4);
  } else
    spi_write((void*)(DirEntry+sort_table[iSelectedEntry]), sizeof(DIRENTRY));

  DisableFpga();

  // prepare transmission of new file
  EnableFpga();
  SPI(DIO_FILE_TX);
  SPI(0xff);
  DisableFpga();
}

static void data_io_file_tx_send(fileTYPE *file) {
  unsigned long bytes2send = file->size;

  /* transmit the entire file using one transfer */
  iprintf("Selected file %.11s with %lu bytes to send\n", file->name, file->size);

  while(bytes2send) {
    iprintf(".");

    unsigned short c, chunk = (bytes2send>512)?512:bytes2send;
    char *p;

    if (rom_direct_upload) {
      // upload directly from the SD-Card if the core supports that
      FileReadEx(file, 0, ((bytes2send-1)>>9)+1);
      bytes2send = 0;
    } else {
      FileRead(file, sector_buffer);

      EnableFpga();
      SPI(DIO_FILE_TX_DAT);

      for(p = sector_buffer, c=0;c < chunk;c++)
        SPI(*p++);

      DisableFpga();
      bytes2send -= chunk;
    }


    // still bytes to send? read next sector
    if(bytes2send)
      FileNextSector(file);
  }
}

static void data_io_file_tx_done(void) {
  // signal end of transmission
  EnableFpga();
  SPI(DIO_FILE_TX);
  SPI(0x00);
  DisableFpga();

  iprintf("\n");
}

static void data_io_file_tx_fill(unsigned char fill, unsigned int len) {

  EnableFpga();
  SPI(DIO_FILE_TX_DAT);
  while(len--) {
    SPI(fill);
  }
  DisableFpga();
}

void data_io_file_tx(fileTYPE *file, unsigned char index) {
  data_io_file_tx_prepare(file, index);
  data_io_file_tx_send(file);
  data_io_file_tx_done();
}

// send 'fill' byte 'len' times
void data_io_fill_tx(unsigned char fill, unsigned int len, unsigned char index) {
  data_io_file_tx_prepare(0, index);
  data_io_file_tx_fill(fill, len);
  data_io_file_tx_done();
}

////////////////////////////
// RECEIVE FILE FROM FPGA //
////////////////////////////

static void data_io_file_rx_prepare(unsigned char index) {
  iprintf("Preparing receiving for index %d\n", index);

  // set index byte (0=bios rom, 1-n=OSD entry index)
  data_io_set_index(index);

  // prepare transmission of new file
  EnableFpga();
  SPI(DIO_FILE_RX);
  SPI(0xff);
  DisableFpga();
}

static void data_io_file_rx_receive(fileTYPE *file, unsigned int len) {
  unsigned long bytes2receive = (len > file->size) ? file->size : len;

  /* receive the entire file using one transfer */
  iprintf("Selected file %.11s with %lu bytes to receive\n", file->name, bytes2receive);

  while(bytes2receive) {
    iprintf(".");

    unsigned short c, chunk = (bytes2receive>512)?512:bytes2receive;
    char *p;

    EnableFpga();
    SPI(DIO_FILE_RX_DAT);
    SPI(0);

    for(p = sector_buffer, c=0;c < chunk;c++)
      *p++ = SPI(0xFF);

    DisableFpga();
    bytes2receive -= chunk;
    FileWrite(file, sector_buffer);

    // still bytes to send? read next sector
    if(bytes2receive)
      FileNextSector(file);
  }
}

static void data_io_file_rx_done(void) {
  // signal end of transmission
  EnableFpga();
  SPI(DIO_FILE_RX);
  SPI(0x00);
  DisableFpga();

  iprintf("\n");
}

void data_io_file_rx(fileTYPE *file, unsigned char index, unsigned int len) {
  data_io_file_rx_prepare(index);
  data_io_file_rx_receive(file, len);
  data_io_file_rx_done();
}

////////////////
// ROM UPLOAD //
////////////////

void data_io_rom_upload(char *rname, char mode) {
  fileTYPE f;
  static char first = 1;
  char s[13];  // 8+3+'\0'

  // new ini parsing starts, prepare uploads
  if(mode == 0) {
    first = 1;
    return;
  }

  // ini parsing done
  if(mode == 2) {
    // has something been uploaded?
    // -> then end transfer
    if(!first) {
      iprintf("upload ends\n");

      data_io_file_tx_done();
      user_io_8bit_set_status(0, UIO_STATUS_RESET);
    }
    return;
  }

  // try to change into core dir. Stay in root if that doesn't exist
  user_io_change_into_core_dir();

  strcpy(s, "        ROM");
  strncpy(s, rname, strlen(rname) < 8 ? strlen(rname) : 8);
  iprintf("rom upload '%s' %d\n", s, sizeof(f));

  if (FileOpenDir(&f, s, iCurrentDirectory)) {
    iprintf("file found!\n");

    if(first) {
      // set reset
      user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
      data_io_file_tx_prepare(&f, 0);
      first = 0;
    }

    //    user_io_file_tx(&f, 0);
    data_io_file_tx_send(&f);
  } else
    iprintf("file not found!\n");

  ChangeDirectory(DIRECTORY_ROOT);
  ScanDirectory(SCAN_INIT, "RBF",  SCAN_LFN | SCAN_SYSDIR);
}
