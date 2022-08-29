#include <FastLED.h>
// #include <WS2812FX.h>
#include "Button2.h";
#include <Adafruit_PN532.h>
#include <SPI.h>
#include <Wire.h>

// Lights
#define LED_COUNT 24
#define LED_PIN 13
#define CHIPSET WS2812
#define COLOR_ORDER GRB
CRGB leds[LED_COUNT];
// WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

// Buttons
#define PIN_BUTTON_A 18
#define PIN_BUTTON_B 19
Button2 buttonA = Button2(PIN_BUTTON_A, INPUT_PULLUP, false, true);
Button2 buttonB = Button2(PIN_BUTTON_B, INPUT_PULLUP, false, true);

// #define PN532_SS   (10)
// #define PN532_MOSI (16)
// #define PN532_MISO (14)
// #define PN532_SCK  (15)
// NFC
#define PN532_SCK (18)
#define PN532_MOSI (23)
#define PN532_SS (5)
#define PN532_MISO (19)
// Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
static const uint8_t KEY_DEFAULT_KEYAB[6] = {0xFF, 0xFF, 0xFF,
                                             0xFF, 0xFF, 0xFF};
// Determine the sector trailer block based on sector number
#define BLOCK_NUMBER_OF_SECTOR_TRAILER(sector)                                 \
  (((sector) < NR_SHORTSECTOR)                                                 \
       ? ((sector)*NR_BLOCK_OF_SHORTSECTOR + NR_BLOCK_OF_SHORTSECTOR - 1)      \
       : (NR_SHORTSECTOR * NR_BLOCK_OF_SHORTSECTOR +                           \
          (sector - NR_SHORTSECTOR) * NR_BLOCK_OF_LONGSECTOR +                 \
          NR_BLOCK_OF_LONGSECTOR - 1))

// RTOS
QueueHandle_t uiQueue;

// Misc
static const char *TAG = "LemonBox";
static const char *DIS_TAG = "LemonBox [dis]";
static const char *BTN_TAG = "LemonBox [btn]";
static const char *NFC_TAG = "LemonBox [nfc]";

struct Message {
  int kind;
};

void clickButtonCallback(Button2 &btn) {
  Message valueToSend = {0};
  if (btn == buttonA) {
    ESP_LOGW(BTN_TAG, "Button pressed: A");
    Serial.println("Button...A");
    valueToSend.kind = 1;
  } else if (btn == buttonB) {
    valueToSend.kind = 2;
    ESP_LOGW(BTN_TAG, "Button pressed: B");
    Serial.println("Button...B");
  }
  xQueueSend(uiQueue, &valueToSend, 1);
}

/*
void TaskNfcReader(void *pvParameters) {
  (void)pvParameters;
  Message valueToSend = {1};

  Serial.println("Starting NFC...");
  nfc.begin();
  Serial.println("Starting NFC...OK");

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.print("Didn't find PN53x board");
    // TODO: Stop the task
    while (1)
      ; // halt
  }

  // Got ok data, print it out!
  Serial.print("Found chip PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  // configure board to read RFID tags
  nfc.SAMConfig();

  for (;;) {

    uint8_t success; // Flag to check if there was an error with the PN532
    uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
    uint8_t uidLength; // Length of the UID (4 or 7 bytes depending on ISO14443A
                       // card type)
    bool authenticated =
        false;               // Flag to indicate if the sector is authenticated
    uint8_t blockBuffer[16]; // Buffer to store block contents
    uint8_t blankAccessBits[3] = {0xff, 0x07, 0x80};
    uint8_t idx = 0;
    uint8_t numOfSector =
        16; // Assume Mifare Classic 1K for now (16 4-block sectors)

    Serial.println(
        "Place your NDEF formatted Mifare Classic 1K card on the reader");
    Serial.println("and press any key to continue ...");

    // Wait for user input before proceeding
    while (!Serial.available())
      ;
    while (Serial.available())
      Serial.read();

    // Wait for an ISO14443A type card (Mifare, etc.).  When one is found
    // 'uid' will be populated with the UID, and uidLength will indicate
    // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

    if (success) {
      // We seem to have a tag ...
      // Display some basic information about it
      Serial.println("Found an ISO14443A card/tag");
      Serial.print("  UID Length: ");
      Serial.print(uidLength, DEC);
      Serial.println(" bytes");
      Serial.print("  UID Value: ");
      nfc.PrintHex(uid, uidLength);
      Serial.println("");

      // Make sure this is a Mifare Classic card
      if (uidLength != 4) {
        Serial.println(
            "Ooops ... this doesn't seem to be a Mifare Classic card!");
        return;
      }

      Serial.println("Seems to be a Mifare Classic card (4 byte UID)");
      Serial.println("");
      Serial.println(
          "Reformatting card for Mifare Classic (please don't touch it!) ... ");

      // Now run through the card sector by sector
      for (idx = 0; idx < numOfSector; idx++) {
        // Step 1: Authenticate the current sector using key B 0xFF 0xFF 0xFF
        // 0xFF 0xFF 0xFF
        success = nfc.mifareclassic_AuthenticateBlock(
            uid, uidLength, BLOCK_NUMBER_OF_SECTOR_TRAILER(idx), 1,
            (uint8_t *)KEY_DEFAULT_KEYAB);
        if (!success) {
          Serial.print("Authentication failed for sector ");
          Serial.println(numOfSector);
          return;
        }

        // Step 2: Write to the other blocks
        if (idx == 16) {
          memset(blockBuffer, 0, sizeof(blockBuffer));
          if (!(nfc.mifareclassic_WriteDataBlock(
                  (BLOCK_NUMBER_OF_SECTOR_TRAILER(idx)) - 3, blockBuffer))) {
            Serial.print("Unable to write to sector ");
            Serial.println(numOfSector);
            return;
          }
        }
        if ((idx == 0) || (idx == 16)) {
          memset(blockBuffer, 0, sizeof(blockBuffer));
          if (!(nfc.mifareclassic_WriteDataBlock(
                  (BLOCK_NUMBER_OF_SECTOR_TRAILER(idx)) - 2, blockBuffer))) {
            Serial.print("Unable to write to sector ");
            Serial.println(numOfSector);
            return;
          }
        } else {
          memset(blockBuffer, 0, sizeof(blockBuffer));
          if (!(nfc.mifareclassic_WriteDataBlock(
                  (BLOCK_NUMBER_OF_SECTOR_TRAILER(idx)) - 3, blockBuffer))) {
            Serial.print("Unable to write to sector ");
            Serial.println(numOfSector);
            return;
          }
          if (!(nfc.mifareclassic_WriteDataBlock(
                  (BLOCK_NUMBER_OF_SECTOR_TRAILER(idx)) - 2, blockBuffer))) {
            Serial.print("Unable to write to sector ");
            Serial.println(numOfSector);
            return;
          }
        }
        memset(blockBuffer, 0, sizeof(blockBuffer));
        if (!(nfc.mifareclassic_WriteDataBlock(
                (BLOCK_NUMBER_OF_SECTOR_TRAILER(idx)) - 1, blockBuffer))) {
          Serial.print("Unable to write to sector ");
          Serial.println(numOfSector);
          return;
        }

        // Step 3: Reset both keys to 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
        memcpy(blockBuffer, KEY_DEFAULT_KEYAB, sizeof(KEY_DEFAULT_KEYAB));
        memcpy(blockBuffer + 6, blankAccessBits, sizeof(blankAccessBits));
        blockBuffer[9] = 0x69;
        memcpy(blockBuffer + 10, KEY_DEFAULT_KEYAB, sizeof(KEY_DEFAULT_KEYAB));

        // Step 4: Write the trailer block
        if (!(nfc.mifareclassic_WriteDataBlock(
                (BLOCK_NUMBER_OF_SECTOR_TRAILER(idx)), blockBuffer))) {
          Serial.print("Unable to write trailer block of sector ");
          Serial.println(numOfSector);
          return;
        }
      }
    }

    // Wait a bit before trying again
    Serial.println("\n\nDone!");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    Serial.flush();
    // while (Serial.available())
    // Serial.read();
  }
}
*/

void TaskButtonReader(void *pvParameters) {
  (void)pvParameters;
  Message valueToSend = {1};

  buttonA.setClickHandler(clickButtonCallback);
  buttonA.setLongClickTime(500);
  buttonB.setClickHandler(clickButtonCallback);
  buttonB.setLongClickTime(500);

  for (;;) {
    buttonA.loop();
    buttonB.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskDisplayControl(void *pvParameters) {
  (void)pvParameters;
  Message valueFromQueue;

  vTaskDelay(500 / portTICK_PERIOD_MS);
  ESP_LOGW(DIS_TAG, "Init display in pin: %d", LED_PIN);
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, LED_COUNT)
      .setCorrection(TypicalSMD5050);
  FastLED.setBrightness(128);

  static uint8_t starthue = 0;

  for (;;) {
    fill_rainbow(leds + 0, LED_COUNT - 0, --starthue, 20);
    FastLED.show();
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // ws2812fx.service();
    if (xQueueReceive(uiQueue, &valueFromQueue, 1) == pdPASS) {
      // // ws2812fx.setMode((ws2812fx.getMode() + 1) %
      // ws2812fx.getModeCount());
    }
  }
}

void setup() {
  Serial.begin(115200);

  // ws2812fx.init();
  // ws2812fx.setBrightness(255);
  // ws2812fx.setSpeed(1000);
  // ws2812fx.setColor(0x007BFF);
  // ws2812fx.setMode(FX_MODE_STATIC);
  // ws2812fx.start();

  uiQueue = xQueueCreate(10, sizeof(Message));
  if (uiQueue != NULL) {
    xTaskCreatePinnedToCore(TaskDisplayControl, "DisplayControl", 2048, NULL, 2,
                            NULL, 0);
    xTaskCreate(TaskButtonReader, "ButtonReader", 1024, NULL, 1, NULL);
    // xTaskCreate(TaskNfcReader, "NfcReader", 2048, NULL, 1, NULL);
  }

  Serial.println("Starting...");
}

void loop() {}
