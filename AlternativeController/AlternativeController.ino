#include <Adafruit_MPR121.h>
#include <Adafruit_SGP30.h>
#include <BleKeyboard.h>
#include <M5Stack.h>
#include <Preferences.h>
#include <VL53L0X.h>
#include <Wire.h>
#include <driver/adc.h>
#include <stdio.h>

#include "MFRC522_I2C.h"

// Unit related constants
const int I2C_ADDR_JOYSTICK = 0x52;
const int I2C_ADDR_VL53L0X = 0x29;
const int I2C_ADDR_SGP30 = 0x58;
const int I2C_ADDR_MPR121 = 0x5B;
const int I2C_ADDR_MFRC522 = 0x28;
const int PIN_PORT_B_A = 36;
const int PIN_PORT_B_B = 26;
const int DIST_RANGE_MIN = 0;
const int DIST_RANGE_MAX = 2000;
const int ECO2_RANGE_MIN = 400;
const int ECO2_RANGE_MAX = 2000;
const int UNIT_NONE = 0;
const int UNIT_DUAL_BUTTON = 1;
const int UNIT_LIGHT = 2;

// Unit related variables
VL53L0X rangingSensor;
Adafruit_SGP30 gasSensor;
MFRC522 rfidReader(I2C_ADDR_MFRC522);
String rfidTagUid[4];
Adafruit_MPR121 touchSensor = Adafruit_MPR121();
bool isDualButtonConnected = false;
bool isLightSensorConnected = false;
bool isJoystickConnected = false;
bool isRangingSensorConnected = false;
bool isGasSensorConnected = false;
bool isRfidReaderConnected = false;
bool isTouchSensorConnected = false;
int unitOnPortB = UNIT_NONE;
int distRangeMin = DIST_RANGE_MIN;
int distRangeMax = DIST_RANGE_MAX;
int eCO2RangeMin = ECO2_RANGE_MIN;
int eCO2RangeMax = ECO2_RANGE_MAX;

// Screen related constants
const int LAYOUT_ANALOG_CH_TOP = 40;
const int LAYOUT_JOYSTICK_CH_TOP = 80;
const int LAYOUT_BUTTONS_CH_TOP = 120;
const int LAYOUT_LINE_HEIGHT = 24;
const int SCREEN_MAIN = 0;
const int SCREEN_PREFS_SELECT = 1;
const int SCREEN_PREFS_EDIT = 2;
const int SCREEN_PREFS_RFID = 3;
const int PREFS_MENU_NUM_ITEMS = 9;
const int PREFS_MENU_INDEX_RFID_1 = 5;
const int PREFS_MENU_INC_DEC_UNIT = 10;

// Screen related variables
int currentMenuItem = 0;
int currentScreenMode = SCREEN_MAIN;
bool isWaitingForNewRfidTag = false;
Preferences preferences;
char analogStatus[30];
char joystickStatus[30];
char buttonsStatus1[30];
char buttonsStatus2[30];

// Protocol related constants
const byte KEYS_FOR_ANALOG_CH[] = {'`', '1', '2', '3', '4', '5',
                                   '6', '7', '8', '9', '0'};
const byte KEYS_FOR_BUTTON_CH[] = {'v', 'b', 'f', 'g', 'r', 't'};
const byte KEY_JOYSTICK_LEFT_UP = 'y';
const byte KEY_JOYSTICK_CENTER_UP = 'u';
const byte KEY_JOYSTICK_RIGHT_UP = 'i';
const byte KEY_JOYSTICK_LEFT = 'h';
const byte KEY_JOYSTICK_CENTER = 'j';
const byte KEY_JOYSTICK_RIGHT = 'k';
const byte KEY_JOYSTICK_LEFT_DOWN = 'n';
const byte KEY_JOYSTICK_CENTER_DOWN = 'm';
const byte KEY_JOYSTICK_RIGHT_DOWN = ',';
const int OFFSET_BUTTON_3 = 2;

// ESP32 BLE Keyboard related variables
// Note: the device name should be within 15 characters;
// otherwise, macOS and iOS devices can't discover
// https://github.com/T-vK/ESP32-BLE-Keyboard/issues/51#issuecomment-733886764
BleKeyboard bleKeyboard("alt. controller");
bool isConnected = false;
bool isSending = false;

void setup() {
  // A workaround to avoid noise regarding Button A
  adc_power_acquire();

  M5.begin();
  M5.Power.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(GREEN, BLACK);
  M5.Lcd.setTextSize(2);

  preferences.begin("alt. controller", false);
  unitOnPortB = preferences.getInt("unitOnPortB", UNIT_NONE);
  distRangeMin = preferences.getInt("distRangeMin", DIST_RANGE_MIN);
  distRangeMax = preferences.getInt("distRangeMax", DIST_RANGE_MAX);
  eCO2RangeMin = preferences.getInt("eCO2RangeMin", ECO2_RANGE_MIN);
  eCO2RangeMax = preferences.getInt("eCO2RangeMax", ECO2_RANGE_MAX);

  rfidTagUid[0] = preferences.getString("rfidTagUid[0]", "**:**:**:**");
  rfidTagUid[1] = preferences.getString("rfidTagUid[1]", "**:**:**:**");
  rfidTagUid[2] = preferences.getString("rfidTagUid[2]", "**:**:**:**");
  rfidTagUid[3] = preferences.getString("rfidTagUid[3]", "**:**:**:**");

  updateFlagsRegardingPortB();

  pinMode(PIN_PORT_B_A, INPUT);
  pinMode(PIN_PORT_B_B, INPUT);

  // Disable the speaker noise
  dacWrite(25, 0);

  Wire.begin();

  // Check if a JOYSTICK Unit is available at the I2C address
  Wire.beginTransmission(I2C_ADDR_JOYSTICK);
  if (Wire.endTransmission() == 0) {
    isJoystickConnected = true;
  }

  if (touchSensor.begin(I2C_ADDR_MPR121)) {
    isTouchSensorConnected = true;
  }

  rangingSensor.setTimeout(500);
  if (rangingSensor.init()) {
    isRangingSensorConnected = true;
    rangingSensor.setMeasurementTimingBudget(20000);
  }

  if (gasSensor.begin()) {
    isGasSensorConnected = true;
  }

  // Check if an RFID Unit is available at the I2C address
  Wire.beginTransmission(I2C_ADDR_MFRC522);
  if (Wire.endTransmission() == 0) {
    rfidReader.PCD_Init();
    isRfidReaderConnected = true;
    sprintf(buttonsStatus2, "RFID Tag:None");
  }

  bleKeyboard.begin();

  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print("Bluetooth: Not connected");

  drawButtons(currentScreenMode);
}

void loop() {
  const unsigned long LOOP_INTERVAL = 25;
  unsigned long start = millis();

  M5.update();
  handleButtons();

  isConnected = bleKeyboard.isConnected();
  bool requestToSend = isConnected && isSending;

  if (isJoystickConnected) {
    handleJoystick(requestToSend);
  }

  if (isDualButtonConnected) {
    handleDualButton(requestToSend);
  }

  if (isTouchSensorConnected) {
    handleTouchSensor(requestToSend);
  } else if (isRfidReaderConnected && !isWaitingForNewRfidTag) {
    handleRFID(requestToSend);
  }

  if (isGasSensorConnected) {
    handleGasSensor(requestToSend);
  } else if (isRangingSensorConnected) {
    handleRangingSensor(requestToSend);
  } else if (isLightSensorConnected) {
    handleAnalogInput(requestToSend);
  }

  if (currentScreenMode == SCREEN_MAIN) {
    drawMainScreen();
  }

  unsigned long now = millis();
  unsigned long elapsed = now - start;

  if (currentScreenMode == SCREEN_MAIN) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(0, 200);
    M5.Lcd.printf("%s", ESP.getSdkVersion());
    M5.Lcd.setCursor(160, 200);
    M5.Lcd.printf("%3d ms", elapsed);
    M5.Lcd.setTextSize(2);
  }

  if (elapsed < LOOP_INTERVAL) {
    delay(LOOP_INTERVAL - elapsed);
  }
}

void handleButtons() {
  if (currentScreenMode != SCREEN_MAIN) {
    M5.Lcd.setCursor(0, 0 + LAYOUT_LINE_HEIGHT * currentMenuItem);
    M5.Lcd.print(">");
  }

  if (M5.BtnA.wasPressed()) {
    switch (currentScreenMode) {
    case SCREEN_MAIN:
      currentScreenMode = SCREEN_PREFS_SELECT;
      M5.Lcd.clear(TFT_BLACK);
      drawButtons(currentScreenMode);
      break;
    case SCREEN_PREFS_SELECT:
      currentScreenMode = SCREEN_MAIN;
      M5.Lcd.clear(TFT_BLACK);
      drawButtons(currentScreenMode);
      break;
    case SCREEN_PREFS_EDIT:
      switch (currentMenuItem) {
      case 0:
        unitOnPortB = unitOnPortB - 1;
        if (unitOnPortB < 0) {
          unitOnPortB = UNIT_LIGHT;
        }
        preferences.putInt("unitOnPortB", unitOnPortB);
        updateFlagsRegardingPortB();
        break;
      case 1:
        eCO2RangeMin = constrain((eCO2RangeMin - PREFS_MENU_INC_DEC_UNIT),
                                 ECO2_RANGE_MIN,
                                 eCO2RangeMax - PREFS_MENU_INC_DEC_UNIT);
        preferences.putInt("eCO2RangeMin", eCO2RangeMin);
        break;
      case 2:
        eCO2RangeMax = constrain((eCO2RangeMax - PREFS_MENU_INC_DEC_UNIT),
                                 eCO2RangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 ECO2_RANGE_MAX);
        preferences.putInt("eCO2RangeMax", eCO2RangeMax);
        break;
      case 3:
        distRangeMin = constrain((distRangeMin - PREFS_MENU_INC_DEC_UNIT),
                                 DIST_RANGE_MIN,
                                 distRangeMax - PREFS_MENU_INC_DEC_UNIT);
        preferences.putInt("distRangeMin", distRangeMin);
        break;
      case 4:
        distRangeMax = constrain((distRangeMax - PREFS_MENU_INC_DEC_UNIT),
                                 distRangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 DIST_RANGE_MAX);
        preferences.putInt("distRangeMax", distRangeMax);
        break;
      default:
        break;
      }
      break;
    default:
      break;
    }
  }

  int rfidTagIdx = currentMenuItem - PREFS_MENU_INDEX_RFID_1;

  if (M5.BtnC.wasPressed()) {
    switch (currentScreenMode) {
    case SCREEN_MAIN:
      if (isSending) {
        isSending = false;
        drawButtons(currentScreenMode);
      } else {
        isSending = true;
        drawButtons(currentScreenMode);
      }
      break;
    case SCREEN_PREFS_SELECT:
      M5.Lcd.setCursor(0, 0 + LAYOUT_LINE_HEIGHT * currentMenuItem);
      M5.Lcd.print(" ");
      currentMenuItem = (currentMenuItem + 1) % PREFS_MENU_NUM_ITEMS;
      M5.Lcd.setCursor(0, 0 + LAYOUT_LINE_HEIGHT * currentMenuItem);
      M5.Lcd.print(">");
      drawButtons(currentScreenMode);
      break;
    case SCREEN_PREFS_RFID:
      rfidTagUid[rfidTagIdx] = "**:**:**:**";
      putRfidTagUidString(rfidTagIdx, rfidTagUid[rfidTagIdx]);
      isWaitingForNewRfidTag = true;
      break;
    case SCREEN_PREFS_EDIT:
      switch (currentMenuItem) {
      case 0:
        unitOnPortB = unitOnPortB + 1;
        if (unitOnPortB > UNIT_LIGHT) {
          unitOnPortB = UNIT_NONE;
        }
        preferences.putInt("unitOnPortB", unitOnPortB);
        updateFlagsRegardingPortB();
        break;
      case 1:
        eCO2RangeMin = constrain((eCO2RangeMin + PREFS_MENU_INC_DEC_UNIT),
                                 ECO2_RANGE_MIN,
                                 eCO2RangeMax - PREFS_MENU_INC_DEC_UNIT);
        preferences.putInt("eCO2RangeMin", eCO2RangeMin);
        break;
      case 2:
        eCO2RangeMax = constrain((eCO2RangeMax + PREFS_MENU_INC_DEC_UNIT),
                                 eCO2RangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 ECO2_RANGE_MAX);
        preferences.putInt("eCO2RangeMax", eCO2RangeMax);
        break;
      case 3:
        distRangeMin = constrain((distRangeMin + PREFS_MENU_INC_DEC_UNIT),
                                 DIST_RANGE_MIN,
                                 distRangeMax - PREFS_MENU_INC_DEC_UNIT);
        preferences.putInt("distRangeMin", distRangeMin);
        break;
      case 4:
        distRangeMax = constrain((distRangeMax + PREFS_MENU_INC_DEC_UNIT),
                                 distRangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 DIST_RANGE_MAX);
        preferences.putInt("distRangeMax", distRangeMax);
        break;
      default:
        break;
      }
      break;
    default:
      break;
    }
  }

  if (M5.BtnA.pressedFor(500)) {
    if (currentScreenMode == SCREEN_PREFS_EDIT) {
      switch (currentMenuItem) {
      case 1:
        eCO2RangeMin = constrain((eCO2RangeMin - PREFS_MENU_INC_DEC_UNIT),
                                 ECO2_RANGE_MIN,
                                 eCO2RangeMax - PREFS_MENU_INC_DEC_UNIT);
        break;
      case 2:
        eCO2RangeMax = constrain((eCO2RangeMax - PREFS_MENU_INC_DEC_UNIT),
                                 eCO2RangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 ECO2_RANGE_MAX);
        break;
      case 3:
        distRangeMin = constrain((distRangeMin - PREFS_MENU_INC_DEC_UNIT),
                                 DIST_RANGE_MIN,
                                 distRangeMax - PREFS_MENU_INC_DEC_UNIT);
        break;
      case 4:
        distRangeMax = constrain((distRangeMax - PREFS_MENU_INC_DEC_UNIT),
                                 distRangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 DIST_RANGE_MAX);
        break;
      default:
        break;
      }
    }
  }

  if (M5.BtnC.pressedFor(500)) {
    if (currentScreenMode == SCREEN_PREFS_EDIT) {
      switch (currentMenuItem) {
      case 1:
        eCO2RangeMin = constrain((eCO2RangeMin + PREFS_MENU_INC_DEC_UNIT),
                                 ECO2_RANGE_MIN,
                                 eCO2RangeMax - PREFS_MENU_INC_DEC_UNIT);
        break;
      case 2:
        eCO2RangeMax = constrain((eCO2RangeMax + PREFS_MENU_INC_DEC_UNIT),
                                 eCO2RangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 ECO2_RANGE_MAX);
        break;
      case 3:
        distRangeMin = constrain((distRangeMin + PREFS_MENU_INC_DEC_UNIT),
                                 DIST_RANGE_MIN,
                                 distRangeMax - PREFS_MENU_INC_DEC_UNIT);
        break;
      case 4:
        distRangeMax = constrain((distRangeMax + PREFS_MENU_INC_DEC_UNIT),
                                 distRangeMin + PREFS_MENU_INC_DEC_UNIT,
                                 DIST_RANGE_MAX);
        break;
      default:
        break;
      }
    }
  }

  if (M5.BtnB.wasPressed()) {
    switch (currentScreenMode) {
    case SCREEN_PREFS_SELECT:
      if (currentMenuItem < PREFS_MENU_INDEX_RFID_1) {
        currentScreenMode = SCREEN_PREFS_EDIT;
      } else {
        currentScreenMode = SCREEN_PREFS_RFID;
      }
      break;
    case SCREEN_PREFS_RFID:
      isWaitingForNewRfidTag = false;
      currentScreenMode = SCREEN_PREFS_SELECT;
      break;
    case SCREEN_PREFS_EDIT:
      currentScreenMode = SCREEN_PREFS_SELECT;
      break;
    default:
      break;
    }

    drawButtons(currentScreenMode);
  }

  if (currentScreenMode != SCREEN_MAIN) {
    drawPreferencesScreen();
  }
}

void updateFlagsRegardingPortB() {
  switch (unitOnPortB) {
  case UNIT_NONE:
    isDualButtonConnected = false;
    isLightSensorConnected = false;
    break;
  case UNIT_DUAL_BUTTON:
    isDualButtonConnected = true;
    isLightSensorConnected = false;
    break;
  case UNIT_LIGHT:
    isDualButtonConnected = false;
    isLightSensorConnected = true;
    break;
  default:
    break;
  }
}

void drawMainScreen() {
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.printf("Bluetooth: %s",
                isConnected ? "Connected    " : "Disconnected ");

  M5.Lcd.setCursor(0, LAYOUT_ANALOG_CH_TOP);
  M5.Lcd.print(analogStatus);

  M5.Lcd.setCursor(0, LAYOUT_JOYSTICK_CH_TOP);
  M5.Lcd.print(joystickStatus);

  M5.Lcd.setCursor(0, LAYOUT_BUTTONS_CH_TOP);
  M5.Lcd.print("BUTTONS:");

  if (isDualButtonConnected) {
    M5.Lcd.setCursor(0, LAYOUT_BUTTONS_CH_TOP + LAYOUT_LINE_HEIGHT);
    M5.Lcd.print(buttonsStatus1);
    M5.Lcd.setCursor(0, LAYOUT_BUTTONS_CH_TOP + LAYOUT_LINE_HEIGHT * 2);
    M5.Lcd.print(buttonsStatus2);
  } else {
    M5.Lcd.setCursor(0, LAYOUT_BUTTONS_CH_TOP + LAYOUT_LINE_HEIGHT);
    M5.Lcd.print(buttonsStatus2);
  }
}

void drawPreferencesScreen() {
  M5.Lcd.setCursor(20, 0 + LAYOUT_LINE_HEIGHT * 0);
  switch (unitOnPortB) {
  case UNIT_NONE:
    M5.Lcd.print("Port B: NONE       ");
    break;
  case UNIT_DUAL_BUTTON:
    M5.Lcd.print("Port B: DUAL BUTTON");
    break;
  case UNIT_LIGHT:
    M5.Lcd.print("Port B: LIGHT      ");
    break;
  default:
    break;
  }

  M5.Lcd.setCursor(20, 0 + LAYOUT_LINE_HEIGHT * 1);
  M5.Lcd.printf("eCO2 Range Min: %5d", eCO2RangeMin);
  M5.Lcd.setCursor(20, 0 + LAYOUT_LINE_HEIGHT * 2);
  M5.Lcd.printf("           Max: %5d", eCO2RangeMax);

  M5.Lcd.setCursor(20, 0 + LAYOUT_LINE_HEIGHT * 3);
  M5.Lcd.printf("Dist Range Min: %5d", distRangeMin);
  M5.Lcd.setCursor(20, 0 + LAYOUT_LINE_HEIGHT * 4);
  M5.Lcd.printf("           Max: %5d", distRangeMax);

  for (int i = 0; i < 4; i++) {
    M5.Lcd.setCursor(20, 0 + LAYOUT_LINE_HEIGHT *
                                 (PREFS_MENU_INDEX_RFID_1 + i));
    M5.Lcd.printf("RFID %d: %s", i + 1, rfidTagUid[i].c_str());
  }

  if (!isRfidReaderConnected) {
    return;
  }

  if (!isWaitingForNewRfidTag) {
    return;
  }

  if (!rfidReader.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfidReader.PICC_ReadCardSerial()) {
    return;
  }

  if (rfidReader.uid.size < 4) {
    return;
  }

  int rfidTagIdx = currentMenuItem - PREFS_MENU_INDEX_RFID_1;
  rfidTagUid[rfidTagIdx] = getRfidTagUidString();
  putRfidTagUidString(rfidTagIdx, rfidTagUid[rfidTagIdx]);

  isWaitingForNewRfidTag = false;

  if (currentScreenMode == SCREEN_PREFS_RFID) {
    currentScreenMode = SCREEN_PREFS_SELECT;
    drawButtons(currentScreenMode);
  }
}

void handleDualButton(bool updateRequested) {
  const int KEY_ID_RED_BUTTON = 0;
  const int KEY_ID_BLUE_BUTTON = 1;

  static bool wasRedButtonPressed = false;
  static bool wasBlueButtonPressed = false;

  bool isRedButtonPressed = digitalRead(PIN_PORT_B_B) == LOW;
  bool isBlueButtonPressed = digitalRead(PIN_PORT_B_A) == LOW;

  sprintf(buttonsStatus1, "Red:%d  Blue:%d", isRedButtonPressed,
          isBlueButtonPressed);

  if (updateRequested) {
    if (!wasRedButtonPressed && isRedButtonPressed) {
      bleKeyboard.press(KEYS_FOR_BUTTON_CH[KEY_ID_RED_BUTTON]);
    } else if (wasRedButtonPressed && !isRedButtonPressed) {
      bleKeyboard.release(KEYS_FOR_BUTTON_CH[KEY_ID_RED_BUTTON]);
    }

    if (!wasBlueButtonPressed && isBlueButtonPressed) {
      bleKeyboard.press(KEYS_FOR_BUTTON_CH[KEY_ID_BLUE_BUTTON]);
    } else if (wasBlueButtonPressed && !isBlueButtonPressed) {
      bleKeyboard.release(KEYS_FOR_BUTTON_CH[KEY_ID_BLUE_BUTTON]);
    }
  }

  wasRedButtonPressed = isRedButtonPressed;
  wasBlueButtonPressed = isBlueButtonPressed;
}

void handleAnalogInput(bool updateRequested) {
  static int lastAnalogValue = -1;

  // convert from 4096 steps to 11 steps
  int sensorReading = analogRead(PIN_PORT_B_A);
  int currentAnalogValue = map(sensorReading, 0, 4095, 0, 10);
  if (lastAnalogValue != currentAnalogValue) {
    sprintf(analogStatus, "ANALOG:%2d (%4d)", currentAnalogValue,
            sensorReading);

    if (updateRequested) {
      bleKeyboard.write(KEYS_FOR_ANALOG_CH[currentAnalogValue]);
    }
    lastAnalogValue = currentAnalogValue;
  }
}

void handleJoystick(bool updateRequested) {
  const byte JOYSTICK_CENTER_RANGE_L = 95;
  const byte JOYSTICK_CENTER_RANGE_H = 159;
  static int lastJoystickX = -2;
  static int lastJoystickY = -2;
  static byte xData = 0;
  static byte yData = 0;
  static byte bData = 0;

  Wire.requestFrom(I2C_ADDR_JOYSTICK, 3);
  if (Wire.available()) {
    xData = Wire.read();
    yData = Wire.read();
    bData = Wire.read();
  }

  int curJoystickX = 0;
  if (xData < JOYSTICK_CENTER_RANGE_L) {
    curJoystickX = -1;
  } else if (xData > JOYSTICK_CENTER_RANGE_H) {
    curJoystickX = 1;
  }

  int curJoystickY = 0;
  if (yData < JOYSTICK_CENTER_RANGE_L) {
    curJoystickY = -1;
  } else if (yData > JOYSTICK_CENTER_RANGE_H) {
    curJoystickY = 1;
  }

  sprintf(joystickStatus, "JOYSTICK:%+d, %+d (%3d, %3d)", curJoystickX,
          curJoystickY, xData, yData);

  if (lastJoystickX != curJoystickX || lastJoystickY != curJoystickY) {
    if (updateRequested) {
      if (curJoystickX == -1 && curJoystickY == 1) {
        bleKeyboard.write(KEY_JOYSTICK_LEFT_UP);
      } else if (curJoystickX == 0 && curJoystickY == 1) {
        bleKeyboard.write(KEY_JOYSTICK_CENTER_UP);
      } else if (curJoystickX == 1 && curJoystickY == 1) {
        bleKeyboard.write(KEY_JOYSTICK_RIGHT_UP);
      } else if (curJoystickX == -1 && curJoystickY == 0) {
        bleKeyboard.write(KEY_JOYSTICK_LEFT);
      } else if (curJoystickX == 0 && curJoystickY == 0) {
        bleKeyboard.write(KEY_JOYSTICK_CENTER);
      } else if (curJoystickX == 1 && curJoystickY == 0) {
        bleKeyboard.write(KEY_JOYSTICK_RIGHT);
      } else if (curJoystickX == -1 && curJoystickY == -1) {
        bleKeyboard.write(KEY_JOYSTICK_LEFT_DOWN);
      } else if (curJoystickX == 0 && curJoystickY == -1) {
        bleKeyboard.write(KEY_JOYSTICK_CENTER_DOWN);
      } else if (curJoystickX == 1 && curJoystickY == -1) {
        bleKeyboard.write(KEY_JOYSTICK_RIGHT_DOWN);
      }
    }

    lastJoystickX = curJoystickX;
    lastJoystickY = curJoystickY;
  }
}

void handleRangingSensor(bool updateRequested) {
  static int lastValue = -1;

  int range = constrain(rangingSensor.readRangeSingleMillimeters(),
                        distRangeMin, distRangeMax);

  // convert to 11 steps
  int currentValue = map(range, distRangeMin, distRangeMax, 0, 10);

  sprintf(analogStatus, "ANALOG:%2d (%4d mm)", currentValue, range);

  if (lastValue != currentValue) {

    if (updateRequested) {
      bleKeyboard.write(KEYS_FOR_ANALOG_CH[currentValue]);
    }
    lastValue = currentValue;
  }
}

void handleGasSensor(bool updateRequested) {
  static int lastValue = -1;

  if (!gasSensor.IAQmeasure()) {
    return;
  }

  int eCO2 = constrain(gasSensor.eCO2, eCO2RangeMin, eCO2RangeMax);

  // convert to 11 steps
  int currentValue = map(eCO2, eCO2RangeMin, eCO2RangeMax, 0, 10);
  sprintf(analogStatus, "ANALOG:%2d (%5d ppm)", currentValue, gasSensor.eCO2);

  if (lastValue != currentValue) {
    if (updateRequested) {
      bleKeyboard.write(KEYS_FOR_ANALOG_CH[currentValue]);
    }
    lastValue = currentValue;
  }
}

// Reference:
// https://github.com/miguelbalboa/rfid/issues/188#issuecomment-495395401
void handleRFID(bool updateRequested) {
  static int lastRfidTag = -1;
  static bool tagPresents = false;
  static bool couldNotSeeTag = false;
  static int consecutiveCountsOfNotSeeTag = 0;

  if (!tagPresents) {
    if (!rfidReader.PICC_IsNewCardPresent()) {
      return;
    }

    if (!rfidReader.PICC_ReadCardSerial()) {
      return;
    }

    if (rfidReader.uid.size < 4) {
      return;
    }

    String curRfidTagUid = getRfidTagUidString();
    for (int i = 0; i < 4; i++) {
      if (rfidTagUid[i] == curRfidTagUid) {
        if (updateRequested) {
          bleKeyboard.press(KEYS_FOR_BUTTON_CH[i + OFFSET_BUTTON_3]);
        }
        sprintf(buttonsStatus2, "RFID Tag:%d   ", i);
        lastRfidTag = i;
        break;
      }
    }

    tagPresents = true;
    couldNotSeeTag = false;
    consecutiveCountsOfNotSeeTag = 0;
  } else {
    bool canNotSeeTag = !rfidReader.PICC_IsNewCardPresent();

    if (canNotSeeTag && couldNotSeeTag) {
      consecutiveCountsOfNotSeeTag++;
    }

    couldNotSeeTag = canNotSeeTag;

    if (consecutiveCountsOfNotSeeTag > 2) {
      if (updateRequested) {
        bleKeyboard.release(KEYS_FOR_BUTTON_CH[lastRfidTag + OFFSET_BUTTON_3]);
      }
      if (currentScreenMode == SCREEN_MAIN) {
        sprintf(buttonsStatus2, "RFID Tag:None");
      }
      lastRfidTag = -1;

      delay(100);
      rfidReader.PICC_HaltA();
      rfidReader.PCD_StopCrypto1();
      tagPresents = false;
    }
  }
}

String getRfidTagUidString() {
  String rfidTagUidString = "";

  for (int i = 0; i < 4; i++) {
    rfidTagUidString += rfidReader.uid.uidByte[i] < 0x10 ? "0" : "";
    rfidTagUidString += String(rfidReader.uid.uidByte[i], HEX);
    if (i < 3) {
      rfidTagUidString += ":";
    }
  }

  return rfidTagUidString;
}

void putRfidTagUidString(int rfidTagIdx, const String &rfidTagUid) {
  String rfidTagUidKey = "rfidTagUid[";
  rfidTagUidKey += String(rfidTagIdx, DEC);
  rfidTagUidKey += "]";
  preferences.putString(rfidTagUidKey.c_str(), rfidTagUid.c_str());
}

void handleTouchSensor(bool updateRequested) {
  static unsigned int lastTouched = 0;

  // Get the currently touched pads
  unsigned int currentlyTouched = touchSensor.touched();
  sprintf(buttonsStatus2, "CH0:%d CH1:%d CH2:%d CH3:%d",
          bitRead(currentlyTouched, 0), bitRead(currentlyTouched, 1),
          bitRead(currentlyTouched, 2), bitRead(currentlyTouched, 3));

  for (int i = 0; i < 4; i++) {
    if (updateRequested) {
      bool wasPadTouched = bitRead(lastTouched, i) == 1;
      bool isPadTouched = bitRead(currentlyTouched, i) == 1;

      if (!wasPadTouched && isPadTouched) {
        bleKeyboard.press(KEYS_FOR_BUTTON_CH[i + OFFSET_BUTTON_3]);
      } else if (wasPadTouched && !isPadTouched) {
        bleKeyboard.release(KEYS_FOR_BUTTON_CH[i + OFFSET_BUTTON_3]);
      }
    }
  }

  lastTouched = currentlyTouched;
}

void drawButtons(int currentScreenMode) {
  const int LAYOUT_BTN_A_CENTER = 64;
  const int LAYOUT_BTN_B_CENTER = 160;
  const int LAYOUT_BTN_C_CENTER = 256;

  switch (currentScreenMode) {
  case SCREEN_MAIN:
    if (!isSending) {
      drawButton(LAYOUT_BTN_A_CENTER, "Setup");
      drawButton(LAYOUT_BTN_B_CENTER, "");
      drawButton(LAYOUT_BTN_C_CENTER, "Send");
    } else {
      drawButton(LAYOUT_BTN_A_CENTER, "Setup");
      drawButton(LAYOUT_BTN_B_CENTER, "");
      drawButton(LAYOUT_BTN_C_CENTER, "Stop");
    }
    break;
  case SCREEN_PREFS_SELECT:
    drawButton(LAYOUT_BTN_A_CENTER, "Exit");
    drawButton(LAYOUT_BTN_B_CENTER, "Go");
    drawButton(LAYOUT_BTN_C_CENTER, "Next");
    break;
  case SCREEN_PREFS_EDIT:
    drawButton(LAYOUT_BTN_A_CENTER, "-");
    drawButton(LAYOUT_BTN_B_CENTER, "Done");
    drawButton(LAYOUT_BTN_C_CENTER, "+");
    break;
  case SCREEN_PREFS_RFID:
    drawButton(LAYOUT_BTN_A_CENTER, "");
    drawButton(LAYOUT_BTN_B_CENTER, "Done");
    drawButton(LAYOUT_BTN_C_CENTER, "Reset");
    break;
  default:
    break;
  }
}

void drawButton(int centerX, const String &title) {
  const int BUTTON_WIDTH = 72;
  const int BUTTON_HEIGHT = 24;

  M5.Lcd.setTextSize(2);

  int fontHeight = M5.Lcd.fontHeight();
  int rectLeft = centerX - BUTTON_WIDTH / 2;
  int rectTop = M5.Lcd.height() - BUTTON_HEIGHT;
  int rectWidth = BUTTON_WIDTH;
  int rectHeight = BUTTON_HEIGHT;
  int coordinateY = rectTop + (rectHeight - fontHeight) / 2;

  M5.Lcd.fillRect(rectLeft, rectTop, rectWidth, rectHeight, TFT_BLACK);
  M5.Lcd.drawRect(rectLeft, rectTop, rectWidth, rectHeight, TFT_GREEN);
  M5.Lcd.drawCentreString(title, centerX, coordinateY, 1);
}
