#include "samd51_psram.h"

static volatile char read_busy, write_busy;
static char data_out[1024], data_in[1024];

static void write_start(void) {
    static unsigned counter = 0;
    snprintf(data_out, sizeof(data_out), "pass %u", counter);

    const unsigned address = (counter++ * 1024) % 8388608;

    const unsigned long micros_before_write = micros();

    write_busy = 1;
    psram_write(data_out, address, sizeof(data_out), &write_busy);

    Serial.printf("%s: psram_write() returned in %lu us\r\n", __func__, micros() - micros_before_write);
}

static void write_finish(void) {
    const unsigned long micros_before_spin = micros();
    while (write_busy);
    Serial.printf("%s: spun for %lu us\r\n", __func__, micros() - micros_before_spin);
}

static void read_start(void) {
    /* put some text in the buffer to show if the transaction fails */
    snprintf(data_in, sizeof(data_in), "fail");

    static unsigned counter = 0;
    const unsigned address = (counter++ * 1024) % 8388608;

    const unsigned long micros_before_read = micros();

    read_busy = 1;
    psram_read(data_in, address, sizeof(data_in), &read_busy);

    Serial.printf("%s: psram_read() returned in %lu us\r\n", __func__, micros() - micros_before_read);
}

static void read_finish(void) {
    /* and sleep until it finishes */
    const unsigned long micros_before_spin = micros();
    while (read_busy);
    Serial.printf("%s: spun for %lu us\r\n", __func__, micros() - micros_before_spin);

    static unsigned counter = 0;
    Serial.printf("%s: expected %u, read back: \"%s\"\r\n\r\n", __func__, counter++, data_in);
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.printf("hello\r\n");

    psram_init();

    /* do one write before looping */
    write_start();
}

void loop() {
    /* wait for previous write to finish (should not block due to the delay) */
    write_finish();

    /* initiate read of the previous write */
    read_start();

    /* immediately attempt to write - this should return immediately, having enqueued
     the transaction such that the write starts when the read finishes */
    write_start();

    /* wait for the read to finish */
    read_finish();

    delay(1000);
}
