/* arduino-based demo of the use of psram as a hard-to-soft-realtime buffer */
#include "samd51_psram.h"

#define PSRAM_SIZE 8388608U

/* keep a tally of unexpected conditions we can print from the main thread */
static size_t fail_count = 0;

/* note this is NOT volatile, readers need to explicitly use __DSB() prior to reading it */
static size_t write_finished;

static void write_start(void) {
    static char data_out[2048];

    static unsigned counter = 0;
    snprintf(data_out, sizeof(data_out), "pass %u", counter++);

    static size_t write_started = 0;
    const unsigned address = write_started % PSRAM_SIZE;
    write_started += sizeof(data_out);

    /* attempt to enqueue a write. this will always return immediately, and will either immediately
     start a transaction, defer one to be started when the previous (write or read) transaction
     completes, or return an error instead of deferring a second pending transaction. since the
     average rate of completion of write transactions has hard real-time requirements, there is
     no point in allowing more than one to be deferred */
    if (-1 == psram_write(data_out, address, sizeof(data_out), &write_finished)) {
        write_finished += sizeof(data_out);
        fail_count++;
    }
}

static void timer_init(void) {
    /* make sure the APB is enabled for TC4 */
    MCLK->APBCMASK.reg |= MCLK_APBCMASK_TC4;

    /* use the 48 MHz clock peripheral */
    GCLK->PCHCTRL[TC4_GCLK_ID].reg = (F_CPU == 48000000 ? GCLK_PCHCTRL_GEN_GCLK0 : GCLK_PCHCTRL_GEN_GCLK1) | GCLK_PCHCTRL_CHEN;
    while (GCLK->SYNCBUSY.reg);

    /* reset the timer peripheral */
    TC4->COUNT8.CTRLA.bit.SWRST = 1;
    while (TC4->COUNT8.SYNCBUSY.bit.SWRST);

    /* put the counter in 8-bit mode */
    TC4->COUNT8.CTRLA.bit.MODE = TC_CTRLA_MODE_COUNT8_Val;

    /* timer ticks will be input clock ticks divided by this prescaler value */
    TC4->COUNT8.CTRLA.bit.PRESCALER = TC_CTRLA_PRESCALER_DIV1024_Val;

    /* timer ticks to this value, inclusive, and then wraps to zero on the next tick */
    TC4->COUNT8.PER.reg = 255;

    NVIC_EnableIRQ(TC4_IRQn);
    NVIC_SetPriority(TC4_IRQn, (1 << __NVIC_PRIO_BITS) - 1);

    TC4->COUNT8.INTENSET.bit.OVF = 1;

    TC4->COUNT8.CTRLA.bit.ENABLE = 1;
    while (TC4->COUNT8.SYNCBUSY.bit.ENABLE);
}

void TC4_Handler(void) {
    if (!TC4->COUNT8.INTFLAG.bit.OVF) return;
    TC4->COUNT8.INTFLAG.reg = TC_INTFLAG_OVF;

    static unsigned slowdown = 0;
    slowdown++;
    if (92 == slowdown) {
        slowdown = 0;
        write_start();
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.printf("hello\r\n");

    psram_init();
    timer_init();
}

void loop() {
    /* these are set in one part of the main thread and read elsewhere */
    static size_t read_started, read_acknowledged;

    /* this is written by an interrupt handler and waited on by the main thread, and needs __DSB() */
    static size_t read_finished;

    static char data_in[2048];

    static unsigned fail_count_acknowledged;

    if (__DSB(), fail_count != fail_count_acknowledged) {
        fail_count_acknowledged = fail_count;
        Serial.printf("%s: fail count now %u\r\n", __func__, fail_count_acknowledged);
    }

    /* if we had initiated a read and it is now finished... */
    if (__DSB(), read_acknowledged != read_finished) {
        static unsigned counter = 0;
        Serial.printf("%s: expected %u, read back: \"%s\"\r\n\r\n", __func__, counter++, data_in);

        read_acknowledged = read_finished;
    }

    __DSB();
    const size_t write_finished_now = write_finished;

    if (read_acknowledged == read_started && write_finished_now - read_started > PSRAM_SIZE - 2 * sizeof(data_in)) {
        read_started = write_finished_now - 2 * sizeof(data_in) - PSRAM_SIZE;
        const size_t bytes_skipped = read_started - read_finished;
        read_finished = read_started;
        read_acknowledged = read_started;
        fail_count += bytes_skipped / sizeof(data_in);
    }

    /* if the previous read has finished, and the writer has finished a newer write... */
    if (__DSB(), read_acknowledged == read_started && read_started != write_finished) {
        /* put some text in the buffer to show if the transaction fails */
        snprintf(data_in, sizeof(data_in), "fail");

        const unsigned address = read_started % PSRAM_SIZE;
        read_started += sizeof(data_in);

        const unsigned long micros_before_read = micros();

        psram_read(data_in, address, sizeof(data_in), &read_finished);

        Serial.printf("%s: %u, %u, psram_read() returned in %lu us\r\n", __func__,
            (unsigned)read_started, (unsigned)write_finished, micros() - micros_before_read);
    }

    /* wait for something to change */
    __WFI();
}
