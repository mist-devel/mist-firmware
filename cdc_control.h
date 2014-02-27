#ifndef CDC_CONTROL_H
#define CDC_CONTROL_H

extern char cdc_control_debug;
extern char cdc_control_redirect;

#define CDC_REDIRECT_RS232    0x01
#define CDC_REDIRECT_PARALLEL 0x02

void cdc_control_open(void);
void cdc_control_poll(void);
void cdc_control_tx(char c);
void cdc_control_flush(void);

#endif // CDC_CONTROL_H
