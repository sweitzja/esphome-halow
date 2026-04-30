/**
 * MM6108 SPI Probe
 *
 * Minimal sketch to verify SPI communication with the Morse Micro MM6108
 * on the XIAO HaLow Hat (WIO-WM6180).
 *
 * The MM6108 uses SDIO-over-SPI. We send CMD52 (IO_RW_DIRECT) to read
 * the CCCR/SDIO revision register at address 0x00 in function 0,
 * and CMD5 (IO_SEND_OP_COND) to verify the chip responds.
 *
 * Pin mapping (from Seeed mm-iot-esp32 Kconfig):
 *   SCK  = GPIO 7
 *   MOSI = GPIO 9
 *   MISO = GPIO 8
 *   CS   = GPIO 4
 *   IRQ  = GPIO 3
 *   RESET_N = GPIO 1
 *   WAKE    = GPIO 2
 *   BUSY    = GPIO 5
 */

#include <Arduino.h>
#include <SPI.h>

// Pin definitions - verified from Seeed mm-iot-esp32 Kconfig
#define PIN_SPI_SCK   7
#define PIN_SPI_MOSI  9
#define PIN_SPI_MISO  8
#define PIN_SPI_CS    4
#define PIN_IRQ       3
#define PIN_RESET_N   1
#define PIN_WAKE      2
#define PIN_BUSY      5

SPIClass mm_spi(HSPI);

// CRC7 for SDIO commands (polynomial x^7 + x^3 + 1)
uint8_t crc7(const uint8_t *data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t d = data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc << 1) | ((d >> 7) & 1);
            d <<= 1;
            if (crc & 0x80) crc ^= 0x09;  // polynomial
        }
    }
    return (crc << 1) | 1;  // end bit
}

// Send an SDIO command over SPI and read response
// Returns the 6-byte response (R1/R5 format)
void sdio_spi_cmd(uint8_t cmd_idx, uint32_t arg, uint8_t *response) {
    uint8_t cmd[6];
    cmd[0] = 0x40 | (cmd_idx & 0x3F);  // start bit + transmission bit + command
    cmd[1] = (arg >> 24) & 0xFF;
    cmd[2] = (arg >> 16) & 0xFF;
    cmd[3] = (arg >>  8) & 0xFF;
    cmd[4] = (arg >>  0) & 0xFF;
    cmd[5] = crc7(cmd, 5);

    digitalWrite(PIN_SPI_CS, LOW);
    delayMicroseconds(10);

    // Send command
    for (int i = 0; i < 6; i++) {
        mm_spi.transfer(cmd[i]);
    }

    // Wait for response (up to 64 clock cycles)
    uint8_t r = 0xFF;
    for (int i = 0; i < 64; i++) {
        r = mm_spi.transfer(0xFF);
        if ((r & 0x80) == 0) {
            // Got start of response
            if (response) response[0] = r;
            for (int j = 1; j < 6; j++) {
                response[j] = mm_spi.transfer(0xFF);
            }
            break;
        }
    }
    if (r == 0xFF && response) {
        // No response
        memset(response, 0xFF, 6);
    }

    delayMicroseconds(10);
    digitalWrite(PIN_SPI_CS, HIGH);

    // 8 clock cycles after CS goes high
    mm_spi.transfer(0xFF);
}

void reset_module() {
    Serial.println("Resetting MM6108...");
    digitalWrite(PIN_RESET_N, LOW);
    delay(100);
    digitalWrite(PIN_RESET_N, HIGH);
    delay(100);

    // Assert WAKE
    digitalWrite(PIN_WAKE, HIGH);
    delay(50);
}

void print_hex(const char *label, const uint8_t *data, int len) {
    Serial.printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println();
    Serial.println("=== MM6108 SPI Probe ===");
    Serial.printf("Pins: SCK=%d MOSI=%d MISO=%d CS=%d\n", PIN_SPI_SCK, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_CS);
    Serial.printf("Pins: RESET=%d WAKE=%d IRQ=%d BUSY=%d\n", PIN_RESET_N, PIN_WAKE, PIN_IRQ, PIN_BUSY);

    // Configure control pins
    pinMode(PIN_SPI_CS, OUTPUT);
    digitalWrite(PIN_SPI_CS, HIGH);

    pinMode(PIN_RESET_N, OUTPUT);
    digitalWrite(PIN_RESET_N, HIGH);

    pinMode(PIN_WAKE, OUTPUT);
    digitalWrite(PIN_WAKE, LOW);

    pinMode(PIN_IRQ, INPUT);
    pinMode(PIN_BUSY, INPUT);

    // Init SPI - MM6108 supports up to 50MHz, start slow at 400kHz for init
    mm_spi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);  // Don't let SPI lib manage CS
    mm_spi.setFrequency(400000);
    mm_spi.setDataMode(SPI_MODE0);
    mm_spi.setBitOrder(MSBFIRST);

    // Reset the module
    reset_module();

    // Send 80+ clock cycles with CS high to enter SPI mode
    Serial.println("Sending 80+ init clocks with CS high...");
    digitalWrite(PIN_SPI_CS, HIGH);
    for (int i = 0; i < 12; i++) {
        mm_spi.transfer(0xFF);
    }
    delay(10);

    uint8_t resp[6];

    // CMD0 - GO_IDLE_STATE (reset to SPI mode)
    Serial.println("\n--- CMD0 (GO_IDLE_STATE) ---");
    sdio_spi_cmd(0, 0, resp);
    print_hex("Response", resp, 6);
    delay(50);

    // CMD5 - IO_SEND_OP_COND with no voltage (query)
    Serial.println("\n--- CMD5 (IO_SEND_OP_COND, query) ---");
    sdio_spi_cmd(5, 0, resp);
    print_hex("Response", resp, 6);
    delay(50);

    // CMD5 again with voltage window 3.2-3.4V
    Serial.println("\n--- CMD5 (IO_SEND_OP_COND, with voltage 0x300000) ---");
    sdio_spi_cmd(5, 0x00300000, resp);
    print_hex("Response", resp, 6);
    delay(50);

    // Try CMD8 - SEND_IF_COND (SD card version check, sometimes helpful)
    Serial.println("\n--- CMD8 (SEND_IF_COND) ---");
    sdio_spi_cmd(8, 0x000001AA, resp);
    print_hex("Response", resp, 6);
    delay(50);

    // CMD52 - Read several CCCR registers
    Serial.println("\n--- CMD52 reads (CCCR registers) ---");
    for (uint32_t addr = 0; addr <= 0x12; addr++) {
        // CMD52 arg format: [31]=R/W, [30:28]=func, [27]=RAW, [25:9]=addr, [7:0]=data
        uint32_t cmd52_arg = (addr << 9);  // Read, func 0
        sdio_spi_cmd(52, cmd52_arg, resp);
        Serial.printf("  CCCR[0x%02X]: ", addr);
        for (int i = 0; i < 6; i++) Serial.printf("%02X ", resp[i]);

        // Decode R5 response: byte 0 = flags, byte 4 = read data
        if (resp[0] != 0xFF) {
            Serial.printf(" (flags=0x%02X data=0x%02X)", resp[0], resp[4]);
        }
        Serial.println();
    }

    // Try reading function 1 (typically the WLAN function) FBR
    Serial.println("\n--- CMD52 reads (Function 1 FBR at 0x100-0x10F) ---");
    for (uint32_t addr = 0x100; addr <= 0x10F; addr++) {
        uint32_t cmd52_arg = (addr << 9);
        sdio_spi_cmd(52, cmd52_arg, resp);
        Serial.printf("  FBR1[0x%03X]: ", addr);
        for (int i = 0; i < 6; i++) Serial.printf("%02X ", resp[i]);
        if (resp[0] != 0xFF) {
            Serial.printf(" (flags=0x%02X data=0x%02X)", resp[0], resp[4]);
        }
        Serial.println();
    }

    // Try raw SPI transfer to see what MISO returns
    Serial.println("\n--- Raw SPI read (16 bytes with CS low) ---");
    digitalWrite(PIN_SPI_CS, LOW);
    delayMicroseconds(10);
    Serial.print("  ");
    for (int i = 0; i < 16; i++) {
        uint8_t b = mm_spi.transfer(0xFF);
        Serial.printf("%02X ", b);
    }
    Serial.println();
    digitalWrite(PIN_SPI_CS, HIGH);
    mm_spi.transfer(0xFF);

    // Pin states
    Serial.printf("\nPin states: IRQ=%d BUSY=%d\n", digitalRead(PIN_IRQ), digitalRead(PIN_BUSY));

    Serial.println("\n=== Probe complete ===");
}

void loop() {
    // Print pin states every 5 seconds
    delay(5000);
    Serial.printf("IRQ=%d BUSY=%d\n", digitalRead(PIN_IRQ), digitalRead(PIN_BUSY));
}
