#ifndef CONFIG_H
#define CONFIG_H

// StarForge Legacy is a RotorHazard timing node. RotorHazard reads SF_FW_VERSION
// from the firmware version string. Keep it aligned with the release tag,
// without a leading v.
#define SF_FW_VERSION "2.0.4"

// Hardware pin definitions - board-specific
#if defined(CONFIG_IDF_TARGET_ESP32C5)
    // ESP32-C5 (single-core RISC-V, 2.4 + 5 GHz radio)
    // By default the C5's own radio is the receiver (SF_RECEIVER_C5_NATIVE,
    // below) and no RX5808 is fitted. The RX5808 pins are still defined so
    // a C5 can drive an external RX5808 with -DSF_RECEIVER_C5_NATIVE=0.
    // Pins avoid the C5 strapping pins (2, 3, 7, 8, 9, 25-28), UART0 (11, 12)
    // and USB (13, 14).
    #define RSSI_INPUT_PIN      1     // GPIO1 (ADC1) - RSSI input (external RX5808 only)
    #define RX5808_DATA_PIN     5     // GPIO5 - DATA (external RX5808 only)
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (external RX5808 only)
    #define RX5808_SEL_PIN      6     // GPIO6 - LE (external RX5808 only)
    #define USE_DMA_ADC         1
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    // GPIO27 on the DevKitC-1 and the C5-Zero is an RGB (WS2812) LED; on the
    // XIAO it is a plain LED, so that environment overrides the style.
    #ifndef STATUS_LED_PIN
        #define STATUS_LED_PIN      27
    #endif
    #ifndef STATUS_LED_WS2812
        #define STATUS_LED_WS2812   1
    #endif
    #define STATUS_LED_INVERTED 0
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB); Wi-Fi uses the rest
    #if defined(BOARD_ESP32_C5_ZERO)
        // Waveshare ESP32-C5-Zero antenna switch: LOW = onboard, HIGH = IPEX connector.
        #define C5_ANT_SELECT_PIN   26
        #ifndef C5_ANT_EXTERNAL
            #define C5_ANT_EXTERNAL 0
        #endif
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    // ESP32-C6 (single-core RISC-V)
    // GPIO3 is used for USB, so RSSI uses GPIO0
    #define RSSI_INPUT_PIN      0     // GPIO0 (ADC1_CH0) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    #define STATUS_LED_PIN      8     // GPIO8 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   20000  // 20,000 samples = 6.6 minutes @ 50Hz (100 KB)
#elif defined(BOARD_NUCLEARCOUNTER)
    // NuclearCounter (ESP32-C3 based board)
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    #define STATUS_LED_PIN      1     // GPIO1 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   20000  // 20,000 samples = 6.6 minutes @ 50Hz (100 KB)
#elif defined(BOARD_ESP32_C3_ZERO)
    // ESP32-C3 Zero - Must come BEFORE generic ESP32-C3 check (more specific)
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    // WS2812 RGB LED on GPIO10 - uses built-in rgbLedWrite()
    #define STATUS_LED_PIN      10    // GPIO10 - WS2812 RGB LED
    #define STATUS_LED_WS2812   1     // Enable WS2812 support
    #define STATUS_LED_INVERTED 0     // Not used for WS2812 (color-based control)
    #define RSSI_HISTORY_SIZE   20000  // 20,000 samples = 6.6 minutes @ 50Hz (100 KB)
#elif defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3 SuperMini (Hertz-hunter compatible)
    // Generic ESP32-C3 - Must come AFTER specific board checks
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    #define STATUS_LED_PIN      8     // GPIO8 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   20000  // 20,000 samples = 6.6 minutes @ 50Hz (100 KB)
#elif defined(BOARD_T_ENERGY)
    // LilyGo T-Energy (ESP32-S3)
    // Available GPIOs: 1-21, 35-48 (avoid 43-46 if using USB/JTAG)
    #define RSSI_INPUT_PIN      4     // GPIO4 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     5     // GPIO5 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      7     // GPIO7 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      6     // GPIO6 - LE (Latch Enable / SPI CS) to RX5808
    #define USE_DMA_ADC         0     // Polled ADC on this board
    #define UART_BAUD_RATE      921600  // USB CDC for serial communication
    #define STATUS_LED_PIN      1     // GPIO1 - Status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB)
#elif defined(BOARD_ESP32_S3_DEVKITC)
    // ESP32-S3-DevKitC-1 (standard development board)
    // Available GPIOs: 1-21, 35-48 (avoid 43-46 if using USB/JTAG)
    #define RSSI_INPUT_PIN      4     // GPIO4 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     11    // GPIO11 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      12    // GPIO12 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      10    // GPIO10 - LE (Latch Enable / SPI CS) to RX5808
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC for serial communication
    #define STATUS_LED_PIN      2     // GPIO2 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB)
#else
    // Generic ESP32 DevKit / ESP32-WROOM-32 (ESP32-D0WD-V3, NodeMCU-32S, etc)
    #define RSSI_INPUT_PIN      34    // GPIO34 (ADC1_CH6) - RSSI input from RX5808 (input only, good for ADC)
    #define RX5808_DATA_PIN     23    // GPIO23 (MOSI) - DATA to RX5808
    #define RX5808_CLK_PIN      18    // GPIO18 (SCK) - CLK to RX5808
    #define RX5808_SEL_PIN      5     // GPIO5 (CS) - LE (Latch Enable) to RX5808
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      115200  // UART bridge baud rate
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB)
    #define RSSI_HISTORY_ENABLED 0     // Disable RSSI history on ESP32-WROOM-32D to avoid cache errors
#endif

// RX5808 frequency constants
#define MIN_FREQ            5645  // Minimum frequency (MHz)
#define MAX_FREQ            5945  // Maximum frequency (MHz)
#define DEFAULT_FREQ        5800  // Default frequency (MHz)

// Timing configuration
#define TIMING_INTERVAL_MS  1     // Core timing loop interval
#define RSSI_SAMPLES        10    // Number of RSSI samples to average (50ms smoothing window) - DEPRECATED: Using Kalman filter instead
#define ENTER_RSSI          120   // Default RSSI threshold to start capturing peak (enter threshold)
#define EXIT_RSSI           100   // Default RSSI threshold to detect lap completion (exit threshold)
#define MIN_LAP_MS          0     // Minimum lap time in milliseconds (0 = disabled, prevents peak capture until elapsed)
#define MIN_LAP_TIME_MS     3000  // Minimum time between laps (3 seconds) - prevents false laps from threshold bouncing

// ============================================
// Native ESP32-C5 receiver
// ============================================
// On an ESP32-C5 the chip's own 5 GHz radio is the receiver: no RX5808, no
// ADC. See src/receiver/c5_radio.* and docs/ESP32_C5.md. Every value below
// can be overridden from platformio.ini build_flags.
#ifndef SF_RECEIVER_C5_NATIVE
  #if defined(CONFIG_IDF_TARGET_ESP32C5)
    #define SF_RECEIVER_C5_NATIVE 1
  #else
    #define SF_RECEIVER_C5_NATIVE 0
  #endif
#endif

#if SF_RECEIVER_C5_NATIVE
  // dBm that reads as RSSI 0 and as 255. Linear in between, like an RX5808.
  #ifndef C5_RSSI_DBM_FLOOR
    #define C5_RSSI_DBM_FLOOR       -100
  #endif
  #ifndef C5_RSSI_DBM_CEIL
    #define C5_RSSI_DBM_CEIL        -20
  #endif
  // Radio reading rate. Filter windows below are in samples (1 ms each).
  #define C5_SAMPLE_RATE_HZ         1000
  // Removes upward spikes shorter than this (background Wi-Fi packets).
  #ifndef C5_FILTER_OPEN_SAMPLES
    #define C5_FILTER_OPEN_SAMPLES  5
  #endif
  // Fills downward dips shorter than this (the VTX reading's own dropouts).
  #ifndef C5_FILTER_CLOSE_SAMPLES
    #define C5_FILTER_CLOSE_SAMPLES 9
  #endif
  // Within this many MHz of a Wi-Fi channel centre, the Wi-Fi driver's own
  // tuning is used; further off, the PHY is moved to the exact frequency.
  #ifndef C5_WIFI_TUNE_TOLERANCE_MHZ
    #define C5_WIFI_TUNE_TOLERANCE_MHZ 1
  #endif
  // Readings are discarded for this long after a retune.
  #ifndef C5_SETTLE_MS
    #define C5_SETTLE_MS            15
  #endif
  // 802.11p receive settings (reported to help above ~5750 MHz). Off until
  // A/B tested on the bench.
  #ifndef C5_ENABLE_11P
    #define C5_ENABLE_11P           0
  #endif
  #ifndef C5_11P_FROM_MHZ
    #define C5_11P_FROM_MHZ         5750
  #endif
  // The radio reading is already cleaned up by the burst filter, so the
  // Kalman filter only needs to smooth the 1 dB steps (about 4 ms).
  #ifndef RSSI_FILTER_Q
    #define RSSI_FILTER_Q           100   // Measurement noise (scaled by 0.01 = 1.0)
  #endif
  #ifndef RSSI_FILTER_R
    #define RSSI_FILTER_R           833   // Process noise (scaled by 0.0001 = 0.0833)
  #endif
#endif

// Kalman filter configuration for RSSI filtering
#ifndef RSSI_FILTER_Q
  #define RSSI_FILTER_Q       2000  // Measurement noise (scaled by 0.01 = 20.0)
#endif
#ifndef RSSI_FILTER_R
  #define RSSI_FILTER_R       40    // Process noise (scaled by 0.0001 = 0.004)
#endif

// ============================================
// Multi-pilot mode (several RotorHazard seats on one board)
// ============================================
// Only a board with the native receiver can do this: one radio hops between
// seats, so it is meaningless with an external RX5808. The C5 environments set
// SF_SEAT_COUNT=8; every other target stays single-seat and unchanged.
#ifndef SF_SEAT_COUNT
  #define SF_SEAT_COUNT 1
#endif
#if SF_SEAT_COUNT > 1 && !SF_RECEIVER_C5_NATIVE
  #error "SF_SEAT_COUNT > 1 needs the native ESP32-C5 receiver (SF_RECEIVER_C5_NATIVE=1)"
#endif
#if SF_SEAT_COUNT > 1
  // Readings are ignored for this long after a retune. Eight pilots on one C5
  // at roughly 1 kHz per seat is established, working practice (RotorHazard
  // team + the reference receiver), which puts the usable retune budget around
  // 125 us per seat. 150 leaves a little margin. Measure it on the bench and
  // tune from here - see docs/ROTORHAZARD_8PILOT.md phase 0.
  #ifndef C5_HOP_SETTLE_US
    #define C5_HOP_SETTLE_US       150
  #endif
  // The burst filter windows are in samples of one seat now, not milliseconds.
  #ifndef C5_MP_FILTER_OPEN
    #define C5_MP_FILTER_OPEN      3
  #endif
  #ifndef C5_MP_FILTER_CLOSE
    #define C5_MP_FILTER_CLOSE     5
  #endif
#endif

// Keep system log text off the RotorHazard serial link (src/system/serial_guard.*).
#ifndef SF_SILENCE_SYSTEM_OUTPUT
  #define SF_SILENCE_SYSTEM_OUTPUT 1
#endif

// FreeRTOS task priority for the timing loop
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #define TIMING_PRIORITY     2     // High priority for timing (critical on single core)
#else
    #define TIMING_PRIORITY     2     // Timing on core 1
#endif

// DMA ADC configuration
// DMA ADC uses ADC1 continuously, which conflicts with analogRead() on ADC1 pins
#define DMA_SAMPLE_RATE     1000  // DMA ADC sample rate in Hz (1000 = 1kHz, matches timing loop)
                                  // ESP32 valid range: 20kHz - 2MHz (ESP32), 1kHz+ (ESP32-C3/S2/S3/C6)
                                  // Lower rate = less CPU overhead, 1kHz is sufficient for RSSI sampling
#define DMA_BUFFER_SIZE     256   // DMA buffer size in samples (larger = more averaging)

// Data storage
#define MAX_LAPS_STORED     50    // Maximum laps to store in memory

// RSSI history buffering
#ifndef RSSI_HISTORY_ENABLED
    #define RSSI_HISTORY_ENABLED    1
#endif
#ifndef RSSI_HISTORY_SIZE
    #define RSSI_HISTORY_SIZE   30000  // Default: 30,000 samples = 10 minutes @ 50Hz (150 KB)
#endif
#define RSSI_SAMPLE_INTERVAL_MS 20    // 50Hz = sample every 20ms

// Debug settings
#define DEBUG_SERIAL        0     // Enable debug output
#define DEBUG_TIMING        0     // Enable timing debug output

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#if DEBUG_TIMING
  #define TIMING_PRINT(x)   Serial.print(x)
  #define TIMING_PRINTLN(x) Serial.println(x)
#else
  #define TIMING_PRINT(x)
  #define TIMING_PRINTLN(x)
#endif

#endif // CONFIG_H
