/*
  esp32cam_audio_receiver.ino

  Kaam: Main ESP32 se UART ke through audio data receive karta hai,
  aur SD card pe WAV file ke roop mein save karta hai.

  Protocol (Main ESP32 se match hona chahiye):
    1. "START\n"           -> naya recording aa raha hai
    2. 4 bytes (uint32)    -> total data length (bytes mein)
    3. raw PCM audio bytes -> actual audio data
    4. "END\n"             -> transfer complete signal

  IMPORTANT: Ye sketch AI-Thinker ESP32-CAM board ke liye hai.
  SD card module already board mein built-in hai.
*/

#include "FS.h"
#include "SD_MMC.h"

#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16
#define CHANNELS 1

int fileCounter = 0;

void setup() {
  Serial.begin(115200);   // Ye UART2 hai jo Main ESP32 se connected hai
  delay(1000);

  Serial.println("ESP32-CAM Audio Receiver starting...");

  if (!SD_MMC.begin()) {
    Serial.println("SD Card mount failed!");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached!");
    return;
  }

  Serial.println("SD Card ready.");

  // Existing files count karo taaki naya file number conflict na kare
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    fileCounter++;
    file = root.openNextFile();
  }
  Serial.println("Ready to receive audio data.");
}

void writeWavHeader(File &file, uint32_t dataLength) {
  uint32_t fileSize = dataLength + 36;
  uint32_t byteRate = SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE / 8;
  uint16_t blockAlign = CHANNELS * BITS_PER_SAMPLE / 8;

  file.write((const uint8_t*)"RIFF", 4);
  file.write((uint8_t*)&fileSize, 4);
  file.write((const uint8_t*)"WAVE", 4);

  file.write((const uint8_t*)"fmt ", 4);
  uint32_t fmtLength = 16;
  file.write((uint8_t*)&fmtLength, 4);
  uint16_t audioFormat = 1; // PCM
  file.write((uint8_t*)&audioFormat, 2);
  uint16_t numChannels = CHANNELS;
  file.write((uint8_t*)&numChannels, 2);
  uint32_t sampleRate = SAMPLE_RATE;
  file.write((uint8_t*)&sampleRate, 4);
  file.write((uint8_t*)&byteRate, 4);
  file.write((uint8_t*)&blockAlign, 2);
  uint16_t bitsPerSample = BITS_PER_SAMPLE;
  file.write((uint8_t*)&bitsPerSample, 2);

  file.write((const uint8_t*)"data", 4);
  file.write((uint8_t*)&dataLength, 4);
}

void receiveAndSaveAudio() {
  // Length read karo (4 bytes)
  uint8_t lengthBytes[4];
  int received = 0;
  unsigned long timeout = millis();

  while (received < 4 && millis() - timeout < 3000) {
    if (Serial.available()) {
      lengthBytes[received++] = Serial.read();
    }
  }

  if (received < 4) {
    Serial.println("Error: length receive timeout");
    return;
  }

  uint32_t dataLength;
  memcpy(&dataLength, lengthBytes, 4);

  Serial.print("Expecting bytes: ");
  Serial.println(dataLength);

  // File banao
  String filename = "/recording_" + String(fileCounter++) + ".wav";
  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Error: file open failed");
    return;
  }

  writeWavHeader(file, dataLength);

  // Audio data receive karo aur file mein likho
  uint32_t bytesReceived = 0;
  timeout = millis();

  while (bytesReceived < dataLength && millis() - timeout < 10000) {
    if (Serial.available()) {
      uint8_t b = Serial.read();
      file.write(b);
      bytesReceived++;
      timeout = millis(); // reset timeout on activity
    }
  }

  file.close();

  Serial.print("Saved: ");
  Serial.print(filename);
  Serial.print(" (");
  Serial.print(bytesReceived);
  Serial.println(" bytes)");

  // "END\n" marker consume karo
  delay(100);
  while (Serial.available()) {
    Serial.read();
  }
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line == "START") {
      Serial.println("Receiving new recording...");
      receiveAndSaveAudio();
    }
  }
}
