#include <Arduino.h>

#include "samd51_psram.h"

#include <stdio.h>
#include <string.h>

static const unsigned long interval_ms = 200;
static unsigned long prev;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(115200);
    while (!Serial);
    Serial.printf("hello\r\n");

    psram_init();

    prev = millis();
}

void loop() {
    const unsigned long now = millis();
    if (now - prev < interval_ms) {
        __WFI();
        return;
    }
    prev += interval_ms;
    /* get here on average every 200 milliseconds */

    static int state = 0;
    state = !state;

    if (1 == state) {
        /* note this is static because it must have a longer lifetime than its scope */
        static char data_out[1024];
        static volatile char data_out_busy = 0;

        /* sleep until PREVIOUS transaction finishes before modifying buffer */
        digitalWrite(LED_BUILTIN, HIGH);
        while (data_out_busy) __WFI();
        digitalWrite(LED_BUILTIN, LOW);
        data_out_busy = 1;

        /* put some text in the buffer */
        static unsigned counter = 0;
        snprintf(data_out, sizeof(data_out), "pass %u", counter++);

        /* kick off the nonblocking transaction */
        psram_write(data_out, 0, sizeof(data_out), &data_out_busy);
    } else {
        static char data_in[1024];
        /* put some text in the buffer to show if the transaction fails */
        snprintf(data_in, sizeof(data_in), "fail");

        /* kick off the non blocking transaction... */
        volatile char busy = 1;
        psram_read(data_in, 0, sizeof(data_in), &busy);

        /* do other work maybe */

        /* and sleep until it finishes */
        digitalWrite(LED_BUILTIN, HIGH);
        while (busy) __WFI();
        digitalWrite(LED_BUILTIN, LOW);

        Serial.printf("read back: \"%s\"\r\n", data_in);
    }
}
