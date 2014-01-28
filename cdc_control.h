#ifndef CDC_CONTROL_H
#define CDC_CONTROL_H

extern char cdc_control_debug;

void cdc_control_open(void);
void cdc_control_poll(void);
void cdc_control_tx(char c, char flush);

#endif // CDC_CONTROL_H
