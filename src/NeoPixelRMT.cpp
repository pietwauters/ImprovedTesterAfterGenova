#include "NeoPixelRMT.h"
#include "esp_log.h"

static const char *TAG = "NeoPixelRMT";

NeoPixelRMT::NeoPixelRMT(uint16_t numPixels, gpio_num_t pin) :
    numPixels(numPixels), pin(pin), channel(RMT_CHANNEL_0),
    pixels(numPixels, 0),
    items(numPixels * BITS_PER_PIXEL + 1) // +1 for reset pulse
{}

NeoPixelRMT::~NeoPixelRMT() {
    rmt_driver_uninstall(channel);
}

void NeoPixelRMT::begin() {
    setRMTConfig();
    rmt_driver_install(channel, 0, 0);
}

void NeoPixelRMT::setRMTConfig() {
    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_TX;
    config.channel = channel;
    config.gpio_num = pin;
    config.mem_block_num = 4;
    config.clk_div = 2;  // 80MHz / 2 = 40MHz (25ns per tick)
    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(channel, 0, 0));
}

void NeoPixelRMT::setPixelColor(uint16_t idx, uint32_t color) {
    if (idx >= numPixels) return;

    // Convert RGB to GRB order for WS2812 LEDs
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    pixels[idx] = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

void NeoPixelRMT::fill(uint32_t color, int startIndex, int count) {
    if (startIndex < 0 || startIndex >= numPixels) return;
    if (count <= 0) return;

    int endIndex = startIndex + count;
    if (endIndex > numPixels) endIndex = numPixels;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint32_t grbColor = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    for (int i = startIndex; i < endIndex; i++) {
        pixels[i] = grbColor;
    }
}


void NeoPixelRMT::clear() {
    std::fill(pixels.begin(), pixels.end(), 0);
}

uint32_t NeoPixelRMT::Color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    if (brightness == 255) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    uint32_t r_scaled = (r * brightness) / 255;
    uint32_t g_scaled = (g * brightness) / 255;
    uint32_t b_scaled = (b * brightness) / 255;

    return (r_scaled << 16) | (g_scaled << 8) | b_scaled;
}

void NeoPixelRMT::encodePixels() {
    int idx = 0;
    for (uint16_t i = 0; i < numPixels; i++) {
        uint32_t color = pixels[i];
        for (int bit = 23; bit >= 0; bit--) {
            bool bitIsSet = color & (1 << bit);
            if (bitIsSet) {
                items[idx].duration0 = T1H;
                items[idx].level0 = 1;
                items[idx].duration1 = T1L;
                items[idx].level1 = 0;
            } else {
                items[idx].duration0 = T0H;
                items[idx].level0 = 1;
                items[idx].duration1 = T0L;
                items[idx].level1 = 0;
            }
            idx++;
        }
    }
    // Reset pulse: low for >50us to latch data
    items[idx].duration0 = RESET_US * 40; // 40 ticks per us (25ns ticks)
    items[idx].level0 = 0;
    items[idx].duration1 = 0;
    items[idx].level1 = 0;
}

void NeoPixelRMT::show() {
    encodePixels();
    gpio_set_level(pin, 0);
    ets_delay_us(80); // 80us reset pulse
    ESP_ERROR_CHECK(rmt_write_items(channel, items.data(), items.size(), true));
    ESP_ERROR_CHECK(rmt_wait_tx_done(channel, pdMS_TO_TICKS(100)));
}
