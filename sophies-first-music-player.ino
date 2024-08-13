#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <Adafruit_NeoPixel.h>
#include <hardware/rtc.h>
#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/rtc.h>

#define VS1053_RESET   -1
#define VS1053_CS       8
#define VS1053_DCS     10
#define CARDCS          7
#define VS1053_DREQ     9

#define KEEP_ALIVE_PIN 6 // GPIO pin for the keep-alive resistor (GP06, D4)
#define KEEP_ALIVE_ON_TIME 80 // Resistor on time in milliseconds
#define KEEP_ALIVE_INTERVAL 20000 // Time between keep-alive pulses in milliseconds
#define SLOW_MODE_VOLTAGE VREG_VOLTAGE_0_90
#define FAST_MODE_VOLTAGE VREG_VOLTAGE_1_10
#define FAST_MODE_MHZ 133
#define SLOW_MODE_MHZ 18

// Keyboard matrix setup
const int ROW_PINS[] = {26, 24, 28, 12, 2};
const int COL_PINS[] = {3, 11, 29};
const int NUM_ROWS = 5;
const int NUM_COLS = 3;

bool isMusicPlaying = false;
unsigned long lastKeepAliveTime = 0;

bool keyStates[NUM_ROWS][NUM_COLS] = {{false}};
unsigned long lastKeyPressTimes[NUM_ROWS][NUM_COLS] = {{0}};
const unsigned long debounceDelay = 50; // milliseconds

bool shiftKeyPressed = false;
unsigned long shiftKeyPressTime = 0;
const unsigned long shiftKeyHoldTime = 300; // milliseconds to consider as "held down"

uint8_t currentVolume = 40; // Starting volume (0-100, where 0 is loudest and 100 is quietest)

Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

void setup() {
  Serial.begin(115200);
  
  // Initialize keyboard matrix
  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(ROW_PINS[i], OUTPUT);
    digitalWrite(ROW_PINS[i], HIGH);
  }
  for (int i = 0; i < NUM_COLS; i++) {
    pinMode(COL_PINS[i], INPUT_PULLUP);
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  pinMode(KEEP_ALIVE_PIN, OUTPUT);
  digitalWrite(KEEP_ALIVE_PIN, LOW);

  if (!musicPlayer.begin()) {
    Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1);
  }

  Serial.println(F("VS1053 found"));
  musicPlayer.sineTest(0x44, 500);

  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);
  }
  Serial.println("SD OK!");

  musicPlayer.setVolume(currentVolume, currentVolume);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

  Serial.println("Ready to play. Press a key to select a song.");
}

void loop() {
  int key = scanKeyboard();
  handleKeyPress(key);

  if (musicPlayer.stopped() && isMusicPlaying) {
    Serial.println("Song finished playing.");
    isMusicPlaying = false;
    digitalWrite(LED_BUILTIN, LOW);
    slowMode();
  }

  // Keep-alive functionality
  if (!isMusicPlaying) {
    unsigned long currentTime = millis();
    if (currentTime - lastKeepAliveTime >= KEEP_ALIVE_INTERVAL) {
      // Perform keep-alive pulse
      digitalWrite(LED_BUILTIN, HIGH);
      digitalWrite(KEEP_ALIVE_PIN, HIGH);
      delay(KEEP_ALIVE_ON_TIME);
      digitalWrite(KEEP_ALIVE_PIN, LOW);
      digitalWrite(LED_BUILTIN, LOW);
      lastKeepAliveTime = currentTime;
      Serial.println("Keep-alive pulse sent");
    }
  }

  delay(50);
}

int scanKeyboard() {
  for (int row = 0; row < NUM_ROWS; row++) {
    digitalWrite(ROW_PINS[row], LOW);
    for (int col = 0; col < NUM_COLS; col++) {
      bool currentState = digitalRead(COL_PINS[col]) == LOW;
      unsigned long currentTime = millis();
      
      if (currentState != keyStates[row][col]) {
        if (currentTime - lastKeyPressTimes[row][col] > debounceDelay) {
          keyStates[row][col] = currentState;
          lastKeyPressTimes[row][col] = currentTime;
          
          int keyNumber = row * NUM_COLS + col + 1;
          digitalWrite(ROW_PINS[row], HIGH);
          return currentState ? keyNumber : -keyNumber; // Positive for press, negative for release
        }
      }
    }
    digitalWrite(ROW_PINS[row], HIGH);
  }
  return 0; // No change detected
}

void handleKeyPress(int key) {
  if (key == 0) return; // No key event

  bool isKeyPress = key > 0;
  int keyNumber = abs(key);

  // Check if it's the shift key (r5c1)
  if (keyNumber == 13) { // 13 is the key number for r5c1 (5 * 3 - 2)
    handleShiftKey(isKeyPress);
    return;
  }

  // Handle other keys
  if (isKeyPress) {
    if (shiftKeyPressed) {
      // Handle shifted functions
      if (keyNumber == 14) { // r5c2
        decreaseVolume();
        delay(300);
      } else if (keyNumber == 15) { // r5c3
        increaseVolume();
        delay(300);
      }
    } else {
      // Normal key function
      handleNormalKeyPress(keyNumber);
    }
  }
}

void handleShiftKey(bool isPressed) {
  if (isPressed) {
    shiftKeyPressTime = millis();
    shiftKeyPressed = true; // Set to true immediately when pressed
  } else {
    if (millis() - shiftKeyPressTime < shiftKeyHoldTime) {
      // Short press, trigger normal function
      handleNormalKeyPress(13); // 13 is the key number for r5c1
    }
    shiftKeyPressed = false;
  }
}

void handleNormalKeyPress(int keyNumber) {
  if (isMusicPlaying) {
    musicPlayer.stopPlaying();
    isMusicPlaying = false;
    Serial.println("Playback stopped.");
    digitalWrite(LED_BUILTIN, LOW);
    slowMode();
  } else {
    fastMode();
    if (playSong(keyNumber))
      digitalWrite(LED_BUILTIN, HIGH);
  }
}

void increaseVolume() {
  if (currentVolume > 16) {
    currentVolume = currentVolume - 8;
    musicPlayer.setVolume(currentVolume, currentVolume);
    Serial.print("Volume increased to: ");
    Serial.println(currentVolume);
  } else {
    Serial.println("Already at maximum volume");
  }
}

void decreaseVolume() {
  if (currentVolume < 48) {
    currentVolume = currentVolume + 8;
    musicPlayer.setVolume(currentVolume, currentVolume);
    Serial.print("Volume decreased to: ");
    Serial.println(currentVolume);
  } else {
    Serial.println("Already at minimum volume");
  }
}

void slowMode() {
  digitalWrite(LED_BUILTIN, LOW);
  set_sys_clock_khz(SLOW_MODE_MHZ * 1000, true);
  vreg_set_voltage(SLOW_MODE_VOLTAGE);
}

void fastMode() {
  vreg_set_voltage(FAST_MODE_VOLTAGE);
  set_sys_clock_khz(FAST_MODE_MHZ * 1000, true);
}

bool playSong(int songNumber) {
  char prefix[10];
  snprintf(prefix, sizeof(prefix), "%d - ", songNumber);
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    return false;
  }

  File file;
  while (file = root.openNextFile()) {
    if (!file.isDirectory()) {
      const char* fileName = file.name();
      
      // Check if the filename starts with a number
      char* endPtr;
      long fileNumber = strtol(fileName, &endPtr, 10);
      
      // If the number matches and is followed by " - "
      if (fileNumber == songNumber && strncmp(endPtr, " - ", 3) == 0) {
        Serial.print("Playing: ");
        Serial.println(fileName);
        musicPlayer.startPlayingFile(fileName);
        isMusicPlaying = true;
        file.close();
        root.close();
        return true;
      }
    }
    file.close();
  }
  
  root.close();
  Serial.println("Song not found");
  return false;
}
