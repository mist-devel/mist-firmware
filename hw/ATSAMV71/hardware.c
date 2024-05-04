/*
This file is part of MiST-firmware

MiST-firmware is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

MiST-firmware is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ctype.h>
#include <stdio.h>
#include "hardware.h"
#include "irq/nvic.h"
#include "utils.h"
#include "mist_cfg.h"
#include "user_io.h"
#include "xmodem.h"

volatile unsigned long timer_ticks = 0;

static void _dummy_handler(void) {}

RAMFUNC void _default_systick_handler()
{
    timer_ticks++;
}

void __init_hardware()
{
    SCB_EnableICache();
    //SCB_EnableDCache();

    SUPC->SUPC_MR = SUPC_MR_BODRSTEN_NOT_ENABLE | SUPC_MR_BODDIS_DISABLE | SUPC_MR_ONREG_ONREG_USED | SUPC_MR_OSCBYPASS_NO_EFFECT | SUPC_MR_KEY_PASSWD;
    WDT->WDT_MR = WDT_MR_WDDIS; // disable watchdog
    RSTC->RSTC_MR = (0xA5 << 24) | RSTC_MR_URSTEN;   // enable external user reset input
    EEFC->EEFC_FMR = EEFC_FMR_FWS(FWS); // Flash wait states

    // *** configure clock generator ****
    // start main crystal oscillator (16MHz)
    PMC->CKGR_MOR = CKGR_MOR_MOSCXTEN | CKGR_MOR_MOSCXTST(0x40) | CKGR_MOR_MOSCRCEN | CKGR_MOR_KEY_PASSWD;
    while (!(PMC->PMC_SR & PMC_SR_MOSCXTS));
    // switch to crystal oscillator
    PMC->CKGR_MOR = CKGR_MOR_MOSCSEL | CKGR_MOR_MOSCXTEN | CKGR_MOR_KEY_PASSWD;
    while (!(PMC->PMC_SR & PMC_SR_MOSCSELS));

    // configure PLL
    PMC->CKGR_PLLAR = CKGR_PLLAR_DIVA(1) | CKGR_PLLAR_MULA(17) | CKGR_PLLAR_PLLACOUNT(0x3f) | CKGR_PLLAR_ONE; // DIV=1 MUL=18 PLLCOUNT=63 (288MHz)
    while (!(PMC->PMC_SR & PMC_SR_LOCKA));
    // configure master clock - prescaler = 1 (288MHz CPU clock), mdiv = 2 (144 MHz peripheral clock)
    PMC->PMC_MCKR = PMC_MCKR_UPLLDIV2 | PMC_MCKR_PRES_CLOCK | PMC_MCKR_CSS_SLOW_CLK;
    while (!(PMC->PMC_SR & PMC_SR_MCKRDY));
    PMC->PMC_MCKR = PMC_MCKR_UPLLDIV2 | PMC_MCKR_PRES_CLOCK | PMC_MCKR_MDIV_PCK_DIV2 | PMC_MCKR_CSS_SLOW_CLK;
    while (!(PMC->PMC_SR & PMC_SR_MCKRDY));
    PMC->PMC_MCKR = PMC_MCKR_UPLLDIV2 | PMC_MCKR_PRES_CLOCK | PMC_MCKR_MDIV_PCK_DIV2 | PMC_MCKR_CSS_PLLA_CLK;
    while (!(PMC->PMC_SR & PMC_SR_MCKRDY));

    // Enable peripheral clock in the PMC for PIO, I2C and DMAC
    PMC->PMC_PCER0 = (1 << ID_PIOA) | (1 << ID_PIOB) | (1 << ID_PIOD) | (1 << ID_TWI0);
    PMC->PMC_PCER1 = (1 << (ID_XDMAC0 - 32));

    // LEDs
    PIOD->PIO_PER = PIO_PER_P28 | PIO_PER_P15;
    PIOD->PIO_OER = PIO_OER_P28 | PIO_OER_P15;
    PIOD->PIO_PUDR = PIO_PUDR_P28 | PIO_PUDR_P15;
    PIOD->PIO_SODR = PIO_SODR_P28 | PIO_SODR_P15; // LEDs OFF

    // initialize SD Card pins
    PIOA->PIO_PDR = PIO_PA25D_MCCK | PIO_PA30C_MCDA0 | PIO_PA31C_MCDA1 | PIO_PA26C_MCDA2 | PIO_PA27C_MCDA3 | PIO_PA28C_MCCDA;
    PIOA->PIO_ABCDSR[0] |= PIO_PA25D_MCCK;
    PIOA->PIO_ABCDSR[1] |= PIO_PA25D_MCCK | PIO_PA30C_MCDA0 | PIO_PA31C_MCDA1 | PIO_PA26C_MCDA2 | PIO_PA27C_MCDA3 | PIO_PA28C_MCCDA;
    PIOD->PIO_PER = SD_CD | SD_WP;
    PIOD->PIO_PPDDR = SD_CD | SD_WP;
    //PIOD->PIO_PUER = SD_CD | SD_WP; // not needed if external pullups are there

    // Configure pins for SPI peripheral use
    PIOD->PIO_PDR = PIO_PD20B_SPI0_MISO | PIO_PD21B_SPI0_MOSI | PIO_PD22B_SPI0_SPCK | PIO_PD25B_SPI0_NPCS1 | PIO_PD12C_SPI0_NPCS2 | PIO_PD27B_SPI0_NPCS3;
    PIOD->PIO_ABCDSR[0] |= PIO_PD20B_SPI0_MISO | PIO_PD21B_SPI0_MOSI | PIO_PD22B_SPI0_SPCK | PIO_PD25B_SPI0_NPCS1 | PIO_PD27B_SPI0_NPCS3;
    PIOD->PIO_ABCDSR[1] &= ~(PIO_PD20B_SPI0_MISO | PIO_PD21B_SPI0_MOSI | PIO_PD22B_SPI0_SPCK | PIO_PD25B_SPI0_NPCS1 | PIO_PD27B_SPI0_NPCS3);
    PIOD->PIO_ABCDSR[0] &= ~PIO_PD12C_SPI0_NPCS2;
    PIOD->PIO_ABCDSR[1] |= PIO_PD12C_SPI0_NPCS2;

    PIOB->PIO_PDR = PIO_PB2D_SPI0_NPCS0;
    PIOB->PIO_ABCDSR[0] |= PIO_PB2D_SPI0_NPCS0;
    PIOB->PIO_ABCDSR[1] |= PIO_PB2D_SPI0_NPCS0;

    // Configure pins for QSPI peripheral use
    PIOA->PIO_PDR = PIO_PA11A_QSPI0_CS | PIO_PA12A_QSPI0_IO1 | PIO_PA13A_QSPI0_IO0 | PIO_PA14A_QSPI0_SCK | PIO_PA17A_QSPI0_IO2;
    PIOD->PIO_PDR = PIO_PD31A_QSPI0_IO3;

    // MAX3421e
    PIOD->PIO_ODR  = USB_INT;

    // I2C
    PIOA->PIO_PDR = PIO_PA3A_TWD0 | PIO_PA4A_TWCK0;
    // clock divider: MCLK/((2**5) * 21 + 3)
    TWI0->TWI_CWGR = TWI_CWGR_CKDIV(5) | TWI_CWGR_CHDIV(21) | TWI_CWGR_CLDIV(21);
    TWI0->TWI_CR = (1<<24) | TWI_CR_HSDIS | TWI_CR_SMBDIS | TWI_CR_SVDIS | TWI_CR_MSEN;
    TWI0->TWI_MMR = 0;

    // Buttons
    BTN_PORT->PIO_ODR = BTN_OSD | BTN_RESET;
    // DIP switches
    PIOD->PIO_ODR = SW1 | SW2;

    // Joystick inputs
    PIOA->PIO_ODR   = JOY0_LEFT | JOY0_RIGHT | JOY0_UP | JOY0_DOWN | JOY0_BTN1 | JOY0_BTN2 | JOY1_LEFT | JOY1_RIGHT | JOY1_UP | JOY1_DOWN | JOY1_BTN1 | JOY1_BTN2;
    PIOA->PIO_PPDDR = JOY0_LEFT | JOY0_RIGHT | JOY0_UP | JOY0_DOWN | JOY0_BTN1 | JOY0_BTN2 | JOY1_LEFT | JOY1_RIGHT | JOY1_UP | JOY1_DOWN | JOY1_BTN1 | JOY1_BTN2;
    PIOA->PIO_PUER  = JOY0_LEFT | JOY0_RIGHT | JOY0_UP | JOY0_DOWN | JOY0_BTN1 | JOY0_BTN2 | JOY1_LEFT | JOY1_RIGHT | JOY1_UP | JOY1_DOWN | JOY1_BTN1 | JOY1_BTN2;
    PIOA->PIO_PER   = JOY0_LEFT | JOY0_RIGHT | JOY0_UP | JOY0_DOWN | JOY0_BTN1 | JOY0_BTN2 | JOY1_LEFT | JOY1_RIGHT | JOY1_UP | JOY1_DOWN | JOY1_BTN1 | JOY1_BTN2;

    // MD select pin - default disabled
    JOY0_SEL_PORT->PIO_ODR = JOY0_SEL_PIN;
    JOY1_SEL_PORT->PIO_ODR = JOY1_SEL_PIN;

    // Configure pins for RMII use
    PIOD->PIO_PDR = PIO_PD0 | PIO_PD1 | PIO_PD2 | PIO_PD3 | PIO_PD4 | PIO_PD5 | PIO_PD6 | PIO_PD7 | PIO_PD8 | PIO_PD9;
    PIOD->PIO_MDER = PIO_PD9; // GMDIO Open-drain
    PIOA->PIO_ODR  = PHY_SIGDET | PHY_INT;
    PIOA->PIO_PPDDR = PHY_SIGDET | PHY_INT;
    PIOA->PIO_ODR = PHY_RESET; // disable, as POR capacitor is on-board
    //PIOA->PIO_MDER = PHY_RESET;
    //PIOA->PIO_CODR = PHY_RESET;
    //PIOA->PIO_PER = PHY_RESET;

#ifdef ALTERA_DCLK
    // altera interface
    PIOD->PIO_SODR = ALTERA_DCLK | ALTERA_DATA0;
    PIOD->PIO_OER  = ALTERA_DCLK | ALTERA_DATA0;
    PIOD->PIO_PER  = ALTERA_DCLK | ALTERA_NCONFIG | ALTERA_NSTATUS | ALTERA_DONE;
    PIOD->PIO_PPDDR = ALTERA_DCLK | ALTERA_NCONFIG | ALTERA_NSTATUS | ALTERA_DATA0 | ALTERA_DONE;
    PIOD->PIO_PUDR = ALTERA_DCLK | ALTERA_NSTATUS | ALTERA_DATA0 | ALTERA_DONE;
    PIOD->PIO_ODR = ALTERA_NCONFIG;
    PIOD->PIO_PUER = ALTERA_NCONFIG;
#endif

    // initialize interrupt vectors
    nvic_initialize(&_dummy_handler);
}

// A buffer of 256 bytes makes index handling pretty trivial
volatile static unsigned char tx_buf[256];
volatile static unsigned char tx_rptr, tx_wptr;

volatile static unsigned char rx_buf[256];
volatile static unsigned char rx_rptr, rx_wptr;

static void Usart0IrqHandler() {

    // Read USART status
    int status = UART0->UART_SR;

    // received something?
    if(status & UART_SR_RXRDY) {
        // read byte from usart
        unsigned char c = UART0->UART_RHR;

        // only store byte if rx buffer is not full
        if((unsigned char)(rx_wptr + 1) != rx_rptr) {
            // there's space in buffer: use it
            rx_buf[rx_wptr++] = c;
        }
    }

    // ready to transmit further bytes?
    if(status & UART_SR_TXRDY) {

        // further bytes to send in buffer? 
        if(tx_wptr != tx_rptr)
        // yes, simply send it and leave irq enabled
        UART0->UART_THR = tx_buf[tx_rptr++];
    else
        // nothing else to send, disable interrupt
        UART0->UART_IDR = UART_IDR_TXRDY;
    }
}

// check usart rx buffer for data
void USART_Poll(void) {
    if(Buttons() & 2)
        xmodem_poll();

    while(rx_wptr != rx_rptr) {
        // this can a little be optimized by sending whole buffer parts 
        // at once and not just single bytes. But that's probably not
        // worth the effort.
        char chr = rx_buf[rx_rptr++];

        if(Buttons() & 2) {
            // if in debug mode use xmodem for file reception
            xmodem_rx_byte(chr);
        } else {
            iprintf("USART RX %d (%c)\n", chr, chr);

            // data available -> send via user_io to core
            user_io_serial_tx(&chr, 1);
        }
    }
}

void USART_Write(unsigned char c) {
#if 0
    while(!(UART0->UART_SR & UART_SR_TXRDY));
    UART0->UART_THR = c;
#else
    if((UART0->UART_SR & UART_SR_TXRDY) && (tx_wptr == tx_rptr)) {
        // transmitter ready and buffer empty? -> send directly
        UART0->UART_THR = c;
    } else {
        // transmitter is not ready: block until space in buffer
        while((unsigned char)(tx_wptr + 1) == tx_rptr);

        // there's space in buffer: use it
        tx_buf[tx_wptr++] = c;
        UART0->UART_IER = UART_IER_TXRDY;  // enable interrupt
    }
#endif
}

void USART_Init(unsigned long baudrate) {

    // Configure PA5 and PA6 for UART0 use
    PIOA->PIO_PDR = PIO_PA9A_URXD0 | PIO_PA10A_UTXD0;

    // Enable the peripheral clock in the PMC
    PMC->PMC_PCER0 = 1 << ID_UART0;

    // Reset and disable receiver & transmitter
    UART0->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;

    // Configure UART mode
    UART0->UART_MR = UART_MR_FILTER_DISABLED | UART_MR_BRSRCCK_PERIPH_CLK | UART_MR_CHMODE_NORMAL | UART_MR_PAR_NO;

    // Configure USART0 rate
    UART0->UART_BRGR = UART_BRGR_CD(MCLK / 16 / baudrate);

    // Enable receiver & transmitter
    UART0->UART_CR = UART_CR_RXEN | UART_CR_TXEN;

    // tx buffer is initially empty
    tx_rptr = tx_wptr = 0;

    // and so is rx buffer
    rx_rptr = rx_wptr = 0;

    // setup NVIC handler
    NVIC_SetVector(ID_UART0, (uint32_t) &Usart0IrqHandler);
    NVIC_EnableIRQ(ID_UART0);
    NVIC_SetPriority(ID_UART0, 0x00);

    UART0->UART_IER = UART_IER_RXRDY;  // enable rx interrupt
}

unsigned long CheckButton(void)
{
    return MenuButton();
}

void timer0_c_irq_handler(void) {
  //* Acknowledge interrupt status
//unsigned int dummy = AT91C_BASE_TC0->TC_SR;

//    ikbd_update_time();
}

void Timer_Init(void) {
    // 1ms systick interrupt
    SysTick->LOAD = PLLCLK/1000;
    SysTick->CTRL = 0x7; // ENABLE/TICKINT/PROC CLKSOURCE
}

RAMFUNC unsigned long GetTimer(unsigned long offset)
{
    return (timer_ticks + offset);
}

RAMFUNC unsigned long CheckTimer(unsigned long time)
{
    return ((time - timer_ticks) > (1UL << 31));
}

void WaitTimer(unsigned long time)
{
    time = GetTimer(time);
    while (!CheckTimer(time));
}

inline char mmc_inserted() {
    return !(PIOD->PIO_PDSR & SD_CD);
}

inline char mmc_write_protected() {
    return !!(PIOD->PIO_PDSR & SD_WP);
}

inline RAMFUNC void MCUReset() {
    RSTC->RSTC_CR = RSTC_CR_PROCRST | RSTC_CR_EXTRST | RSTC_CR_KEY_PASSWD;
}

inline int GetRTTC() {return timer_ticks;}

void InitRTTC() {
  // reprogram the realtime timer to run at 1Khz
  //AT91C_BASE_RTTC->RTTC_RTMR = 0x8000 / 1000;
}

int GetSPICLK() {
  return (MCLK / ((SPI0->SPI_CSR[0] & SPI_CSR_SCBR_Msk) >> SPI_CSR_SCBR_Pos) / 1000000);
}

// not used
void InitADC(void) {}

// not used
void PollADC() {}

// user, menu, DIP1, DIP2
unsigned char Buttons() {
    unsigned char map = 0;
    if (!(BTN_PORT->PIO_PDSR & BTN_RESET)) map |= 0x08;
    if (!(BTN_PORT->PIO_PDSR & BTN_OSD)) map |= 0x04;
    if (!(PIOD->PIO_PDSR & SW1)) map |= 0x02;
    if (!(PIOD->PIO_PDSR & SW2)) map |= 0x01;
    return (map);
}

unsigned char MenuButton() {
    return (!(BTN_PORT->PIO_PDSR & BTN_OSD));
}

unsigned char UserButton() {
    return (!(BTN_PORT->PIO_PDSR & BTN_RESET));
}

static char md_state[2] = {0, 0};
static char md_detected = 0;
static unsigned long md_timer[2] = {0, 0};

void InitDB9() {
    md_state[0] = md_state[1] = 0;
    if (mist_cfg.joystick_db9_md) {
        md_detected = 0x03;
        iprintf("Activating Mega Drive DB9 select pin\n");
        JOY0_SEL_PORT->PIO_PUDR = JOY0_SEL_PIN; // deactivate pullup
        JOY0_SEL_PORT->PIO_CODR = JOY0_SEL_PIN; // clear output
        JOY0_SEL_PORT->PIO_OER  = JOY0_SEL_PIN; // enable output
        JOY1_SEL_PORT->PIO_PUDR = JOY0_SEL_PIN; // deactivate pullup
        JOY1_SEL_PORT->PIO_CODR = JOY1_SEL_PIN; // clear output
        JOY1_SEL_PORT->PIO_OER  = JOY1_SEL_PIN; // enable output
    } else {
        md_detected = 0;
        JOY0_SEL_PORT->PIO_ODR = JOY0_SEL_PIN;  // disable output
        JOY0_SEL_PORT->PIO_PUER = JOY0_SEL_PIN; // activate pullup to not leave the pin floating
        JOY1_SEL_PORT->PIO_ODR = JOY1_SEL_PIN;  // disable output
        JOY1_SEL_PORT->PIO_PUER = JOY0_SEL_PIN; // activate pullup to not leave the pin floating
    }
}

// poll db9 joysticks
static uint8_t GetJoyState(char index) {
    uint8_t joymap = 0;
    if (!index) {
        int state = JOY0;
        if(!(state & JOY0_UP))    joymap |= JOY_UP;
        if(!(state & JOY0_DOWN))  joymap |= JOY_DOWN;
        if(!(state & JOY0_LEFT))  joymap |= JOY_LEFT;
        if(!(state & JOY0_RIGHT)) joymap |= JOY_RIGHT;
        if(!(state & JOY0_BTN1))  joymap |= JOY_BTN1;
        if(!(state & JOY0_BTN2))  joymap |= JOY_BTN2;
    } else {
        int state = JOY1;
        if(!(state & JOY1_UP))    joymap |= JOY_UP;
        if(!(state & JOY1_DOWN))  joymap |= JOY_DOWN;
        if(!(state & JOY1_LEFT))  joymap |= JOY_LEFT;
        if(!(state & JOY1_RIGHT)) joymap |= JOY_RIGHT;
        if(!(state & JOY1_BTN1))  joymap |= JOY_BTN1;
        if(!(state & JOY1_BTN2))  joymap |= JOY_BTN2;
    }
    return joymap;
}

#define MD_PAD(index) (md_detected & (0x01<<index))

//Cycle TH out TR in TL in D3 in D2 in D1 in D0 in
//1     HI     C      B    Right Left  Down  Up
//2     LO     Start  A    0     0     Down  Up
//3     HI     C      B    Right Left  Down  Up
//4     LO     Start  A    0     0     Down  Up
//5     HI     C      B    Right Left  Down  Up
//6     LO     Start  A    0     0     0     0
//7     HI     C      B    Mode  X     Y     Z
//8     LO     Start  A    ---   ---   ---   ---

static char GetDB9State(char index, uint16_t *joy_map) {

    static uint8_t joy_state_0[2] = {0, 0}, joy_state_1[2] = {0, 0}, joy_state_2[2] = {0, 0};
    uint8_t state = GetJoyState(index);
    if(md_state[index] == 0x03) {
       md_state[index] = 0x01;
       // skip state 8
       return 0;
    }
    //iprintf("index=%d, md_state=%d, state=%02x, state[0]=%02x state[1]=%02x state[2]=%02x\n", index, md_state[index], state, joy_state_0[0], joy_state_1[0], joy_state_2[0]);
    if((md_state[index] == 0 && state != joy_state_0[index]) || (md_state[index] == 0x02 && state != joy_state_2[index]) || (md_state[index] == 0x01 && state != joy_state_1[index])) {
        if (md_state[index] == 0x01) {
            // TH lo
            if ((state & (JOY_LEFT | JOY_RIGHT | JOY_UP | JOY_DOWN)) == (JOY_LEFT | JOY_RIGHT | JOY_UP | JOY_DOWN)) {
                // 6 button controller detected at state 6
                md_state[index] |= 0x02;
                return 0;
            }
            joy_state_1[index] = state;
        } else if (md_state[index] == 0x02) {
            // TH hi at state 7
            joy_state_2[index] = state;
        } else
            // TH hi
            joy_state_0[index] = state;

        *joy_map = 0;
        if((joy_state_0[index] & JOY_UP))    *joy_map |= JOY_UP;
        if((joy_state_0[index] & JOY_DOWN))  *joy_map |= JOY_DOWN;
        if((joy_state_0[index] & JOY_LEFT))  *joy_map |= JOY_LEFT;
        if((joy_state_0[index] & JOY_RIGHT)) *joy_map |= JOY_RIGHT;
        if((joy_state_0[index] & JOY_BTN1))  *joy_map |= MD_PAD(index) ? JOY_BTN2 : JOY_BTN1;
        if((joy_state_0[index] & JOY_BTN2))  *joy_map |= MD_PAD(index) ? JOY_SELECT : JOY_BTN2;
        if((joy_state_1[index] & JOY_BTN1))  *joy_map |= JOY_BTN1;
        if((joy_state_1[index] & JOY_BTN2))  *joy_map |= JOY_START;
        if((joy_state_2[index] & JOY_UP))    *joy_map |= JOY_L; //Z
        if((joy_state_2[index] & JOY_DOWN))  *joy_map |= JOY_Y;
        if((joy_state_2[index] & JOY_LEFT))  *joy_map |= JOY_X;
        if((joy_state_2[index] & JOY_RIGHT)) *joy_map |= JOY_R; //MODE

        if(mist_cfg.joystick_db9_md == 2 && md_state[index] == 0x01 && MD_PAD(index) && ((joy_state_1[index] & (JOY_LEFT | JOY_RIGHT)) != (JOY_LEFT | JOY_RIGHT))) {
            iprintf("Deactivating Mega Drive DB9 select pin for port %d\n", index);
            if (index) {
                JOY1_SEL_PORT->PIO_ODR = JOY1_SEL_PIN;  // disable output
                JOY1_SEL_PORT->PIO_PUER = JOY1_SEL_PIN; // activate pullup to not leave the pin floating
            } else {
                JOY0_SEL_PORT->PIO_ODR = JOY0_SEL_PIN;  // disable output
                JOY0_SEL_PORT->PIO_PUER = JOY0_SEL_PIN; // activate pullup to not leave the pin floating
            }
            md_detected &= ~(0x01<<index);
            md_state[index] &= ~0x01;
            joy_state_1[index]=joy_state_2[index] = 0;
        }

        return 1;
    } else
        return 0;
}


char GetDB9(char index, uint16_t *joy_map) {

/*
    if (MD_PAD(index) && (!(md_state[index] & 0x01))) {
        if (!CheckTimer(md_timer[index])) return 0;
        else md_timer[index] == GetTimer(20);
    }
*/
    char ret = GetDB9State(index, joy_map);

    // switch SEL pin
    if (MD_PAD(index)) {
        md_state[index] ^= 0x01;
        if (md_state[index] & 0x01) {
            if (index)
                JOY1_SEL_PORT->PIO_CODR = JOY1_SEL_PIN;
            else
                JOY0_SEL_PORT->PIO_CODR = JOY0_SEL_PIN;
        } else {
            if (index)
                JOY1_SEL_PORT->PIO_SODR = JOY1_SEL_PIN;
            else
                JOY0_SEL_PORT->PIO_SODR = JOY0_SEL_PIN;
        }
    }
    return ret;
}

// DS1307: sec, min, hour, day, date, month, year
// MiST:   year, month, date, hour, min, sec, day
static int RTC_offsets[] = {5, 4, 3, 6, 2, 1, 0};

char GetRTC(unsigned char *d) {
    uint32_t status;
    TWI0->TWI_MMR = TWI_MMR_MREAD | TWI_MMR_DADR(0x68) | TWI_MMR_IADRSZ_1_BYTE;
    TWI0->TWI_IADR = 0;
    TWI0->TWI_CR |= TWI_CR_START;
    for (int i = 0; i < 7; i++) {
        do {
            status = TWI0->TWI_SR;
            if (status & (TWI_SR_NACK | TWI_SR_UNRE | TWI_SR_OVRE | TWI_SR_ARBLST)) {
                iprintf("GetRTC error: %08x\n", status);
                TWI0->TWI_CR |= TWI_CR_STOP;
                if (status & TWI_SR_RXRDY) TWI0->TWI_RHR;
                return 0;
            }
        } while(!(status & TWI_SR_RXRDY));
        d[RTC_offsets[i]] = bcd2bin(TWI0->TWI_RHR);
        if (i == 5) TWI0->TWI_CR |= TWI_CR_STOP;
    }
    while(!(TWI0->TWI_SR & TWI_SR_TXCOMP));
    return 1;
}

char SetRTC(unsigned char *d) {
    uint32_t status;
    uint8_t data;
    TWI0->TWI_MMR = TWI_MMR_DADR(0x68) | TWI_MMR_IADRSZ_1_BYTE;
    TWI0->TWI_IADR = 0;
    for (int i = 0; i < 7; i++) {
        do {
            status = TWI0->TWI_SR;
            if (status & (TWI_SR_NACK | TWI_SR_UNRE | TWI_SR_OVRE | TWI_SR_ARBLST)) {
                iprintf("SetRTC error: %08x\n", status);
                TWI0->TWI_CR |= TWI_CR_STOP;
                return 0;
            }
        } while(!(status & TWI_SR_TXRDY));
        data = d[RTC_offsets[i]];
        if (i<6) data &= 0x7F; // make sure CH is cleared
        TWI0->TWI_THR = bin2bcd(data);
    }
    TWI0->TWI_CR |= TWI_CR_STOP;
    while(!(TWI0->TWI_SR & TWI_SR_TXRDY));
    while(!(TWI0->TWI_SR & TWI_SR_TXCOMP));
    return 1;
}

void RAMFUNC UnlockFlash() {
    for (int i = 0; i < 64; i++) {
        while (!(EEFC->EEFC_FSR & EEFC_FSR_FRDY));  // wait for ready
        EEFC->EEFC_FCR = EEFC_FCR_FCMD_CLB | EEFC_FCR_FARG(i) | EEFC_FCR_FKEY_PASSWD; // unlock page
        while (!(EEFC->EEFC_FSR & EEFC_FSR_FRDY));  // wait for ready
    }
}

void RAMFUNC WriteFlash(unsigned long page) {
    uint32_t status;
    while (!(EEFC->EEFC_FSR & EEFC_FSR_FRDY));  // wait for ready
    if (!(page & 0xf)) {
        EEFC->EEFC_FCR = EEFC_FCR_FCMD_EPA | EEFC_FCR_FARG(0x02 | page) | EEFC_FCR_FKEY_PASSWD; // erase 16 pages
        while (!(EEFC->EEFC_FSR & EEFC_FSR_FRDY));  // wait for ready
    }

    EEFC->EEFC_FCR = EEFC_FCR_FCMD_WP | EEFC_FCR_FARG(page) | EEFC_FCR_FKEY_PASSWD; // write page
    while (!(EEFC->EEFC_FSR & EEFC_FSR_FRDY));  // wait for ready
}
