/*
  Standalone Voice Assistant - Single ESP32-CAM (Temporary, sample collection ke liye)
  - INMP441 Microphone (I2S)
  - TTP223 Touch Sensor (GPIO 4)
  - SH1106 OLED Display (U8g2 Library on GPIO 14 & 15 - safer pins, GPIO 12/13 avoid kiya)
  - Built-in SD Card WAV Saving (1-bit mode)

  NOTE: Ye pin choices is liye hain kyunki abhi camera module CONNECT NAHI hai.
  Jab camera lagana ho fire-detection ke liye, ye pins phir se conflict karenge
  aur alag se plan karna hoga.
*/

#include <driver/i2s.h>
#include "FS.h"
#include "SD_MMC.h"
#include <Wire.h>
#include <U8g2lib.h>

// ---- OLED Display Setup (SH1106) - GPIO 12/13 se shift kiya safer pins pe ----
U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 14, /* data=*/ 15, /* reset=*/ U8X8_PIN_NONE);

// ---- Mic Pins (INMP441) ----
#define I2S_MIC_SD   32
#define I2S_MIC_WS   25
#define I2S_MIC_SCK  26
#define I2S_PORT_MIC I2S_NUM_0

// ---- TTP223 Touch Sensor Input - GPIO 16 se GPIO 4 kiya (safer) ----
#define TOUCH_PIN 4

#define SAMPLE_RATE      16000
#define SAMPLE_BUFFER    512
#define RECORD_SECONDS   2
#define TOTAL_SAMPLES (SAMPLE_RATE * RECORD_SECONDS)

int16_t audioBuffer[TOTAL_SAMPLES];
int32_t rawSamples[SAMPLE_BUFFER];
int fileCounter = 0;

void displayStatus(String msg) {
  Serial.println(msg);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(5, 35, msg.c_str());
  u8g2.sendBuffer();
}

void setupMic() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = SAMPLE_BUFFER,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  i2s_driver_install(I2S_PORT_MIC, &config, 0, NULL);
  i2s_set_pin(I2S_PORT_MIC, &pins);
}

void writeWavHeader(File &file, uint32_t dataLength) {
  uint32_t fileSize = dataLength + 36;
  uint32_t byteRate = SAMPLE_RATE * 1 * 16 / 8;
  uint16_t blockAlign = 1 * 16 / 8;

  file.write((const uint8_t*)"RIFF", 4);
  file.write((uint8_t*)&fileSize, 4);
  file.write((const uint8_t*)"WAVE", 4);
  file.write((const uint8_t*)"fmt ", 4);
  uint32_t fmtLength = 16;
  file.write((uint8_t*)&fmtLength, 4);
  uint16_t audioFormat = 1;
  file.write((uint8_t*)&audioFormat, 2);
  uint16_t numChannels = 1;
  file.write((uint8_t*)&numChannels, 2);
  uint32_t sampleRate = SAMPLE_RATE;
  file.write((uint8_t*)&sampleRate, 4);
  file.write((uint8_t*)&byteRate, 4);
  file.write((uint8_t*)&blockAlign, 2);
  uint16_t bitsPerSample = 16;
  file.write((uint8_t*)&bitsPerSample, 2);
  file.write((const uint8_t*)"data", 4);
  file.write((uint8_t*)&dataLength, 4);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TOUCH_PIN, INPUT);

  u8g2.begin();
  displayStatus("Initializing...");
  delay(1000);

  if (!SD_MMC.begin("/sdcard", true)) {
    displayStatus("SD Card Failed!");
    return;
  }

  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    fileCounter++;
    file = root.openNextFile();
  }

  setupMic();
  displayStatus("Ready! Touch TTP223");
}

void loop() {
  if (digitalRead(TOUCH_PIN) == HIGH) {

    displayStatus("Recording...");

    int sampleIndex = 0;
    while (sampleIndex < TOTAL_SAMPLES) {
      size_t bytesRead = 0;
      i2s_read(I2S_PORT_MIC, rawSamples, sizeof(rawSamples), &bytesRead, portMAX_DELAY);
      int samplesRead = bytesRead / sizeof(int32_t);

      for (int i = 0; i < samplesRead && sampleIndex < TOTAL_SAMPLES; i++) {
        int32_t sample32 = rawSamples[i] >> 14;
        if (sample32 > 32767) sample32 = 32767;
        if (sample32 < -32768) sample32 = -32768;
        audioBuffer[sampleIndex++] = (int16_t)sample32;
      }
    }

    displayStatus("Saving to SD...");

    String filename = "/recording_" + String(fileCounter++) + ".wav";
    File file = SD_MMC.open(filename, FILE_WRITE);
    if (file) {
      uint32_t dataLength = TOTAL_SAMPLES * sizeof(int16_t);
      writeWavHeader(file, dataLength);

      file.write((uint8_t*)audioBuffer, dataLength);
      file.close();

      displayStatus("Saved! Ready.");
    } else {
      displayStatus("Save Failed!");
    }

    while (digitalRead(TOUCH_PIN) == HIGH) {
      delay(100);
    }

    delay(500);
    displayStatus("Ready! Touch TTP223");
  }
}
