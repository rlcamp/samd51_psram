#include "samd51_psram.h"

#if __has_include(<samd51.h>)
/* newer cmsis-atmel from upstream */
#include <samd51.h>
#else
/* older cmsis-atmel from adafruit */
#include <samd.h>
#endif

#include <assert.h>

/* use dma channels that have the same interrupt, so as to place nice with more critical stuff */
#define ICHANNEL_SPI_WRITE 5
#define ICHANNEL_SPI_READ 4

__attribute__((weak, aligned(16))) SECTION_DMAC_DESCRIPTOR DmacDescriptor dmac_descriptors[8] = { 0 }, dmac_writeback[8] = { 0 };

void psram_init(void) {
    /* sercom3 pad 2 is miso, pad 1 is sck, pad 3 is mosi. hw cs is not used */

    /* configure pin PA20 (arduino pin 10 on the feather m4) as output for cs pin */
    PORT->Group[0].OUTSET.reg = 1U << 20;
    PORT->Group[0].PINCFG[20].reg = 0;
    PORT->Group[0].DIRSET.reg = 1U << 20;

    /* configure pin PA16 ("5" on feather m4) to use functionality D (sercom3 pad 1), drive strength 1, for sck */
    PORT->Group[0].PINCFG[16] = (PORT_PINCFG_Type) { .bit = { .PMUXEN = 1, .DRVSTR = 1 } };
    PORT->Group[0].PMUX[16 >> 1].bit.PMUXE = 0x3;

    /* configure pin PA19 ("9" on feather m4) to use functionality D (sercom3 pad 3), drive strength 1, for mosi */
    PORT->Group[0].PINCFG[19] = (PORT_PINCFG_Type) { .bit = { .PMUXEN = 1, .DRVSTR = 1 } };
    PORT->Group[0].PMUX[19 >> 1].bit.PMUXO = 0x3;

    /* configure pin PA18 ("6" on feather m4) to use functionality D (sercom3 pad 2), input enabled, for miso */
    PORT->Group[0].PINCFG[18] = (PORT_PINCFG_Type) { .bit = { .PMUXEN = 1, .INEN = 1 } };
    PORT->Group[0].PMUX[18 >> 1].bit.PMUXE = 0x3;

    MCLK->APBBMASK.reg |= MCLK_APBBMASK_SERCOM3;

    /* unconditionally assume GCLK0 is running at F_CPU */
    GCLK->PCHCTRL[SERCOM3_GCLK_ID_CORE].bit.CHEN = 0;
    while (GCLK->PCHCTRL[SERCOM3_GCLK_ID_CORE].bit.CHEN);
    GCLK->PCHCTRL[SERCOM3_GCLK_ID_CORE].reg = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN;
    while (!GCLK->PCHCTRL[SERCOM3_GCLK_ID_CORE].bit.CHEN);

    /* reset spi peripheral */
    SERCOM3->SPI.CTRLA.bit.SWRST = 1;
    while (SERCOM3->SPI.CTRLA.bit.SWRST || SERCOM3->SPI.SYNCBUSY.bit.SWRST);

    SERCOM3->SPI.CTRLA = (SERCOM_SPI_CTRLA_Type) { .bit = {
        .MODE = 0x3, /* spi peripheral is in master mode */
        .DOPO = 0x2, /* clock is sercom pad 1, MOSI is pad 3 */
        .DIPO = 0x2, /* MISO is sercom pad 2 */
        .CPOL = 0, /* sck is low when idle */
        .CPHA = 0,
        .DORD = 0, /* msb first */
    }};

    SERCOM3->SPI.CTRLB = (SERCOM_SPI_CTRLB_Type) { .bit = {
        .RXEN = 0, /* spi receive is not enabled yet */
        .MSSEN = 0, /* no hardware cs control */
        .CHSIZE = 0 /* eight bit characters */
    }};
    while (SERCOM3->SPI.SYNCBUSY.bit.CTRLB);

    const uint32_t baudrate = F_CPU / 4;
    SERCOM3->SPI.BAUD.reg = F_CPU / (2U * baudrate) - 1U;

#if 1
    /* enable spi peripheral */
    SERCOM3->SPI.CTRLA.bit.ENABLE = 1;
    while (SERCOM3->SPI.SYNCBUSY.bit.ENABLE);

    /* lower ss pin */
    PORT->Group[0].OUTCLR.reg = 1U << 20;

    SERCOM3->SPI.DATA.reg = 0x66;
    while (!SERCOM3->SPI.INTFLAG.bit.TXC);

    SERCOM3->SPI.DATA.reg = 0x99;
    while (!SERCOM3->SPI.INTFLAG.bit.TXC);

    /* raise ss pin */
    PORT->Group[0].OUTSET.reg = 1U << 20;

    /* enable spi peripheral */
    SERCOM3->SPI.CTRLA.bit.ENABLE = 0;
    while (SERCOM3->SPI.SYNCBUSY.bit.ENABLE);
#endif

    /* if dma has not yet been initted... */
    if (!DMAC->BASEADDR.bit.BASEADDR) {
        /* init ahb clock for dmac */
        MCLK->AHBMASK.bit.DMAC_ = 1;

        DMAC->CTRL.bit.DMAENABLE = 0;
        DMAC->CTRL.bit.SWRST = 1;

        DMAC->BASEADDR.bit.BASEADDR = (unsigned long)dmac_descriptors;
        DMAC->WRBADDR.bit.WRBADDR = (unsigned long)dmac_writeback;

        /* re-enable dmac */
        DMAC->CTRL.reg = DMAC_CTRL_DMAENABLE | DMAC_CTRL_LVLEN(0xF);
    }

    /* reset channel */
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.ENABLE = 0;
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.SWRST = 1;

    /* clear sw trigger */
    DMAC->SWTRIGCTRL.reg &= ~(1 << ICHANNEL_SPI_WRITE);

    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.RUNSTDBY = 1;
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.TRIGSRC = 0x0B; /* trigger when sercom3 is ready to send a new byte */
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.TRIGACT = DMAC_CHCTRLA_TRIGACT_BURST_Val; /* transfer one byte when triggered */
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.BURSTLEN = DMAC_CHCTRLA_BURSTLEN_SINGLE_Val; /* one burst = one beat */
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHINTENSET.bit.TCMPL = 1; /* fire interrupt on completion (defined in descriptor) */

    /* must agree with ICHANNEL_SPI_READ */
    static_assert(4 == ICHANNEL_SPI_READ, "dmac channel isr mismatch");
    NVIC_EnableIRQ(DMAC_4_IRQn);
    NVIC_SetPriority(DMAC_4_IRQn, (1 << __NVIC_PRIO_BITS) - 1);

    /* reset channel */
    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.ENABLE = 0;
    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.SWRST = 1;

    /* clear sw trigger */
    DMAC->SWTRIGCTRL.reg &= ~(1 << ICHANNEL_SPI_READ);

    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.RUNSTDBY = 1;
    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.TRIGSRC = 0x0A; /* trigger when sercom3 has received one new byte */
    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.TRIGACT = DMAC_CHCTRLA_TRIGACT_BURST_Val; /* transfer one byte when triggered */
    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.BURSTLEN = DMAC_CHCTRLA_BURSTLEN_SINGLE_Val; /* one burst = one beat */
    DMAC->Channel[ICHANNEL_SPI_READ].CHINTENSET.bit.TCMPL = 1; /* fire interrupt on completion (defined in descriptor) */

    /* enable spi peripheral */
    SERCOM3->SPI.CTRLA.bit.ENABLE = 1;
    while (SERCOM3->SPI.SYNCBUSY.bit.ENABLE);
}

static char busy = 0;

static size_t * read_increment_when_done_p = NULL;
static size_t * write_increment_when_done_p = NULL;

static const void * deferred_write_data = NULL;
static unsigned long deferred_write_address;
static size_t deferred_write_count;
static size_t * deferred_write_increment_when_done_p = NULL;

static int psram_write_unlocked(const void * const data, const unsigned long address, const size_t count, size_t * increment_when_done_p) {
    /* assume we cannot get here unless busy was logically zero */
    busy = 1;
    write_increment_when_done_p = increment_when_done_p;

    /* make sure the spi receiver is disabled */
    SERCOM3->SPI.CTRLB.bit.RXEN = 0;
    while (SERCOM3->SPI.SYNCBUSY.bit.CTRLB);

    /* lower ss pin */
    PORT->Group[0].OUTCLR.reg = 1U << 20;

    __attribute__((aligned(16))) SECTION_DMAC_DESCRIPTOR static DmacDescriptor second_tx_descriptor;
    second_tx_descriptor = (DmacDescriptor) {
        .BTCNT.reg = count,
        .SRCADDR.reg = ((size_t)data) + count,
        .DSTADDR.reg = (size_t)&(SERCOM3->SPI.DATA.reg),
        .BTCTRL = {
            .bit.VALID = 1,
            .bit.SRCINC = 1, /* inc from (srcaddr.reg - count) to (srcadddr.reg - 1) inclusive */
            .bit.DSTINC = 0, /* write to the same register every time */
            .bit.BLOCKACT = DMAC_BTCTRL_BLOCKACT_INT_Val, /* fire an interrupt after every block */
        }
    };

    static unsigned char command[4] = { 0x02 };
    command[1] = address >> 16;
    command[2] = address >> 8;
    command[3] = address;

    *(((DmacDescriptor *)DMAC->BASEADDR.bit.BASEADDR) + ICHANNEL_SPI_WRITE) = (DmacDescriptor) {
        .DESCADDR.reg = (size_t)&second_tx_descriptor,
        .BTCNT.reg = 4, /* number of beats in transaction, where one beat is one byte */
        .SRCADDR.reg = ((size_t)command) + 4, /* note this points one past the last byte */
        .DSTADDR.reg = (size_t)&(SERCOM3->SPI.DATA.reg),
        .BTCTRL = {
            .bit.VALID = 1,
            .bit.SRCINC = 1, /* inc from (srcaddr.reg - count) to (srcadddr.reg - 1) inclusive */
            .bit.DSTINC = 0, /* write to the same register every time */
        }
    };

    /* make sure dma descriptor update has completed prior to enabling */
    __DSB();

    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.ENABLE = 0;

    /* setting this starts the transaction */
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.ENABLE = 1;

    return 0;
}

int psram_write(const void * const data, const unsigned long address, const size_t count, size_t * const increment_when_done_p) {
    /* in the use case where one of these is initiated by an interrupt handler at regular intervals
     shorter than the expected length of a write, but read access is initiated irregularly from the
     main thread, we want writes to never block. therefore we will allow one write to be deferred
     and automatically initiated when the read is finished */
    if (!busy) return psram_write_unlocked(data, address, count, increment_when_done_p);

    /* fail if more than one deferred write is attempted */
    else if (deferred_write_data) return -1;

    else {
        deferred_write_address = address;
        deferred_write_count = count;
        deferred_write_data = data;
        deferred_write_increment_when_done_p = increment_when_done_p;

        return 0;
    }
}

void psram_read(void * const data, const unsigned long address, const size_t count, size_t * const increment_when_done_p) {
    /* wait for previous send to finish */
    while (busy) { __DSB(); __WFE(); }

    busy = 1;

    read_increment_when_done_p = increment_when_done_p;

    /* make sure the spi receiver is enabled */
    SERCOM3->SPI.CTRLB.bit.RXEN = 1;
    while (SERCOM3->SPI.SYNCBUSY.bit.CTRLB);

    /* lower ss pin */
    PORT->Group[0].OUTCLR.reg = 1U << 20;

    /* dummy values we need to take addresses of from within the dma subsystem. fixme? */
    static unsigned char zero = 0, garbage = 0;

    /* descriptor for data portion of the tx transaction, which just clocks out a bunch of zeros */
    __attribute__((aligned(16))) SECTION_DMAC_DESCRIPTOR static DmacDescriptor second_tx_descriptor;
    second_tx_descriptor = (DmacDescriptor) {
        .BTCNT.reg = count,
        .SRCADDR.reg = (size_t)&zero,
        .DSTADDR.reg = (size_t)&(SERCOM3->SPI.DATA.reg),
        .BTCTRL = {
            .bit.VALID = 1,
            .bit.SRCINC = 0,
            .bit.DSTINC = 0,
            .bit.BLOCKACT = DMAC_BTCTRL_BLOCKACT_INT_Val, /* fire an interrupt after every block */
        }
    };

    static unsigned char command[4] = { 0x03 };
    command[1] = address >> 16;
    command[2] = address >> 8;
    command[3] = address;

    *(((DmacDescriptor *)DMAC->BASEADDR.bit.BASEADDR) + ICHANNEL_SPI_WRITE) = (DmacDescriptor) {
        .DESCADDR.reg = (size_t)&second_tx_descriptor,
        .BTCNT.reg = 4, /* number of beats in transaction, where one beat is one byte */
        .SRCADDR.reg = ((size_t)command) + 4, /* note this points one past the last byte */
        .DSTADDR.reg = (size_t)&(SERCOM3->SPI.DATA.reg),
        .BTCTRL = {
            .bit.VALID = 1,
            .bit.SRCINC = 1, /* inc from (srcaddr.reg - count) to (srcadddr.reg - 1) inclusive */
            .bit.DSTINC = 0, /* write to the same register every time */
        }
    };

    /* descriptor for the data portion of the rx transaction */
    __attribute__((aligned(16))) SECTION_DMAC_DESCRIPTOR static DmacDescriptor second_rx_descriptor;
    second_rx_descriptor = (DmacDescriptor) {
        .BTCNT.reg = count, /* number of beats in transaction, where one beat is one byte */
        .SRCADDR.reg = (size_t)&(SERCOM3->SPI.DATA.reg),
        .DSTADDR.reg = ((size_t)data) + count,
        .BTCTRL = {
            .bit.VALID = 1,
            .bit.SRCINC = 0, /* read from the same register every time */
            .bit.DSTINC = 1, /* increment destination register after every byte */
            .bit.BLOCKACT = DMAC_BTCTRL_BLOCKACT_INT_Val, /* fire an interrupt */
        }
    };

    /* descriptor for first four bytes of the rx transaction, which just discards four bytes */
    *(((DmacDescriptor *)DMAC->BASEADDR.bit.BASEADDR) + ICHANNEL_SPI_READ) = (DmacDescriptor) {
        .DESCADDR.reg = (size_t)&second_rx_descriptor,
        .BTCNT.reg = 4, /* number of beats in transaction, where one beat is one byte */
        .SRCADDR.reg = (size_t)&(SERCOM3->SPI.DATA.reg),
        .DSTADDR.reg = (size_t)&garbage,
        .BTCTRL = {
            .bit.VALID = 1,
            .bit.SRCINC = 0, /* read from the same register every time */
            .bit.DSTINC = 0, /* write to same register every time */
        }
    };

    /* need to ensure descriptors are written to sram before enabling channels */
    __DSB();

    /* nothing happens immediately when this is enabled */
    DMAC->Channel[ICHANNEL_SPI_READ].CHCTRLA.bit.ENABLE = 1;

    /* enabling this starts the whole transaction */
    DMAC->Channel[ICHANNEL_SPI_WRITE].CHCTRLA.bit.ENABLE = 1;
}

/* note these interrupt handlers cannot have static linkage */
static_assert(4 == ICHANNEL_SPI_READ, "dmac channel isr mismatch");
static_assert(5 == ICHANNEL_SPI_WRITE, "dmac channel isr mismatch");

/* dma channels 4 through 31 all share the same interrupt */
void DMAC_4_Handler(void) {
    const size_t ic = DMAC->INTPEND.bit.ID;
    if (!(DMAC->Channel[ic].CHINTFLAG.reg & DMAC_CHINTENCLR_TCMPL)) return; /* should never happen */

    if (ICHANNEL_SPI_READ == ic) {
      /* fires when a read transaction is finished */
        DMAC->Channel[ic].CHINTFLAG.reg = DMAC_CHINTENCLR_TCMPL;

        /* notify main thread that receiving has finished */
        if (read_increment_when_done_p) {
            (*read_increment_when_done_p)++;
            read_increment_when_done_p = NULL;
        }
    }

    else if (ICHANNEL_SPI_WRITE == ic) {
        /* fires when the last outgoing byte has been enqueued */
        DMAC->Channel[ic].CHINTFLAG.reg = DMAC_CHINTENCLR_TCMPL;

        /* spin for not more than 16 or 40 cycles (at 48 MHz or 120 MHz cpu respectively) */
        while (!SERCOM3->SPI.INTFLAG.bit.TXC);

        /* raise ss pin */
        PORT->Group[0].OUTSET.reg = 1U << 20;

        /* we get here when completing a write OR read, but this can only be non-NULL for a write */
        if (write_increment_when_done_p) {
            (*write_increment_when_done_p)++;
            write_increment_when_done_p = NULL;
        }

        /* if no additional pending write, notify main thread that transmitting has finished */
        if (!deferred_write_data) busy = 0;
        else {
            /* otherwise consume the pending write, and start it without having lowered the busy flag */
            psram_write_unlocked(deferred_write_data, deferred_write_address, deferred_write_count, deferred_write_increment_when_done_p);

            deferred_write_data = NULL;
        }
    }
    __DSB();
}
