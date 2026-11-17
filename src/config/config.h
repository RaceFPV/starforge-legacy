#ifndef CONFIG_H
#define CONFIG_H

// StarForge Legacy is a RotorHazard timing node. RotorHazard reads SF_FW_VERSION
// from the firmware version string. Keep it aligned with the release tag,
// without a leading v.
#define SF_FW_VERSION "2.0.4"

// Hardware pin definitions - board-specific
#if defined(CONFIG_IDF_TARGET_ESP32C6)
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

// Kalman filter configuration for RSSI filtering
#define RSSI_FILTER_Q       2000  // Measurement noise (scaled by 0.01 = 20.0)
#define RSSI_FILTER_R       40    // Process noise (scaled by 0.0001 = 0.004)

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
