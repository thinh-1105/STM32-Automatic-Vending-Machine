#include <STM32FreeRTOS.h>
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
// =====================================================
// 1. LCD I2C: SDA = PB9, SCL = PB8
// =====================================================

LiquidCrystal_I2C lcd(0x27, 20, 4);

// =====================================================
// 2. KEYPAD 4x4
// =====================================================

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {PA0, PA1, PA2, PA3};
byte colPins[COLS] = {PA4, PA5, PA6, PA7};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// =====================================================
// 3. COIN SENSOR - EXTERNAL INTERRUPT
// =====================================================

const uint8_t COIN_PIN = PB0;

const uint32_t COIN_DEBOUNCE_US = 400000UL;

volatile bool coinAcceptEnabled = false;
volatile uint16_t coinCount = 0;
volatile uint32_t lastCoinInterruptUs = 0;

// =====================================================
// 4. HC-SR04
// TRIG = PB7, ECHO = PB6
// =====================================================

const uint8_t TRIG_PIN = PB7;
const uint8_t ECHO_PIN = PB6;

const float ITEM_PRESENT_CM = 8.0f;
const float ITEM_REMOVED_CM = 12.0f;

const uint32_t PRODUCT_TIMEOUT_MS = 15000UL;
const uint32_t REMOVE_TIMEOUT_MS = 30000UL;

volatile float distanceCm = 999.0f;

// =====================================================
// 5. FOUR DC MOTORS - TWO L298N
// =====================================================

const uint8_t motorPins[4][2] = {
  {PB5, PB4},
  {PB3, PA15},
  {PA12, PA11},
  {PA10, PA9}
};

// =====================================================
// 6. PRODUCT PRICES
// =====================================================

const uint8_t prices[4] = {2, 2, 3, 3};

const uint32_t VND_PER_COIN = 1000UL;

// =====================================================
// 7. FINITE STATE MACHINE
// =====================================================

enum MachineState {
  SELECT_PRODUCT,
  CONFIRM_PRODUCT,
  INSERT_COINS,
  DISPENSING,
  WAIT_PICKUP,
  ERROR_STATE,
  COMPLETE_STATE,
  CANCEL_NOTICE_STATE
};

MachineState state = SELECT_PRODUCT;

int selectedProduct = -1;

uint16_t displayedCoins = 0;
uint16_t currentCoins = 0;

uint32_t stateStartMs = 0;
uint32_t ignoreKeysUntil = 0;

// =====================================================
// 8. FREERTOS CONFIGURATION
// =====================================================

// TaskFSM priority cao hơn TaskSensor.

const UBaseType_t PRIORITY_FSM = 2;
const UBaseType_t PRIORITY_SENSOR = 1;

// =====================================================
// 9. COIN SENSOR INTERRUPT
// =====================================================

void coinSensorISR() {

  if (!coinAcceptEnabled) {
    return;
  }

  uint32_t nowUs = micros();

  if ((uint32_t)(nowUs - lastCoinInterruptUs)
      >= COIN_DEBOUNCE_US) {

    if (coinCount < 65535) {
      coinCount++;
    }

    lastCoinInterruptUs = nowUs;
  }
}

// Read coin count safely.

uint16_t getCoins() {

  uint16_t result;

  taskENTER_CRITICAL();

  result = coinCount;

  taskEXIT_CRITICAL();

  return result;
}

// Clear coin balance.

void coinClear() {

  taskENTER_CRITICAL();

  coinCount = 0;

  taskEXIT_CRITICAL();
}

// Subtract coins after successful delivery.

void coinSubtract(uint16_t amount) {

  taskENTER_CRITICAL();

  if (coinCount >= amount) {
    coinCount -= amount;
  } else {
    coinCount = 0;
  }

  taskEXIT_CRITICAL();
}

// Enable coin sensor without deleting saved balance.

void coinStart() {

  taskENTER_CRITICAL();

  lastCoinInterruptUs = micros() - COIN_DEBOUNCE_US;

  coinAcceptEnabled = true;

  taskEXIT_CRITICAL();
}

// Disable coin sensor.

void coinStop() {

  taskENTER_CRITICAL();

  coinAcceptEnabled = false;

  taskEXIT_CRITICAL();
}

// =====================================================
// 10. SENSOR DISTANCE
// =====================================================

float getDistance() {

  return distanceCm;
}

// =====================================================
// 11. MOTOR CONTROL
// ONLY TaskFSM CONTROLS MOTORS
// =====================================================

void stopAllMotors() {

  for (uint8_t i = 0; i < 4; i++) {

    digitalWrite(motorPins[i][0], LOW);
    digitalWrite(motorPins[i][1], LOW);
  }
}

void motorForward(uint8_t motor) {

  if (state != DISPENSING || motor >= 4) {
    return;
  }

  digitalWrite(motorPins[motor][0], HIGH);
  digitalWrite(motorPins[motor][1], LOW);
}

void motorStop(uint8_t motor) {

  if (motor >= 4) {
    return;
  }

  digitalWrite(motorPins[motor][0], LOW);
  digitalWrite(motorPins[motor][1], LOW);
}

// =====================================================
// 12. LCD DISPLAY
// ONLY TaskFSM WRITES LCD
// =====================================================

void showMenu() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("MAY BAN HANG");

  lcd.setCursor(0, 1);
  lcd.print("So du: ");
  lcd.print(getCoins());
  lcd.print(" xu");

  lcd.setCursor(0, 2);
  lcd.print("Nhan phim 1-4");

  lcd.setCursor(0, 3);
  lcd.print("De chon san pham");
}

void showProduct() {

  if (selectedProduct < 0 || selectedProduct > 3) {
    return;
  }

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SAN PHAM DA CHON");

  lcd.setCursor(0, 1);
  lcd.print("SP");
  lcd.print(selectedProduct + 1);

  lcd.print(": ");
  lcd.print(prices[selectedProduct]);
  lcd.print(" xu / ");

  lcd.print(
    (uint32_t)prices[selectedProduct] * VND_PER_COIN
  );

  lcd.setCursor(0, 2);
  lcd.print("So du: ");
  lcd.print(getCoins());
  lcd.print(" xu");

  lcd.setCursor(0, 3);
  lcd.print("B: MUA  C: HUY");
}

void showPayment(uint16_t amount) {

  if (selectedProduct < 0 || selectedProduct > 3) {
    return;
  }

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("DANG BO XU - SP");
  lcd.print(selectedProduct + 1);

  lcd.setCursor(0, 1);
  lcd.print("Da bo: ");
  lcd.print(amount);
  lcd.print("/");
  lcd.print(prices[selectedProduct]);
  lcd.print(" xu");

  if (amount >= prices[selectedProduct]) {

    lcd.setCursor(0, 2);
    lcd.print("DA DU XU");

    lcd.setCursor(0, 3);
    lcd.print("#: MUA C: HUY");

  } else {

    lcd.setCursor(0, 2);
    lcd.print("Moi bo xu vao khe");

    lcd.setCursor(0, 3);
    lcd.print("C: HUY GIAO DICH");
  }
}

void showDispensing() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("DANG XUAT SAN PHAM");

  lcd.setCursor(0, 1);
  lcd.print("San pham: SP");
  lcd.print(selectedProduct + 1);

  lcd.setCursor(0, 2);
  lcd.print("Dong co dang chay");
}

void showWaitingPickup() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("VUI LONG LAY HANG");

  lcd.setCursor(0, 1);
  lcd.print("Da thay san pham");

  lcd.setCursor(0, 2);
  lcd.print("Dang doi lay hang");
}

void showCancelBalance() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("DA HUY GIAO DICH");

  lcd.setCursor(0, 1);
  lcd.print("Xu duoc bao luu:");

  lcd.setCursor(0, 2);
  lcd.print(getCoins());
  lcd.print(" xu");

  lcd.setCursor(0, 3);
  lcd.print("Chon SP tiep theo");
}

// =====================================================
// 13. TRANSACTION CONTROL
// =====================================================

void resetTransaction() {

  coinStop();

  stopAllMotors();

  selectedProduct = -1;

  displayedCoins = getCoins();
  currentCoins = displayedCoins;

  state = SELECT_PRODUCT;

  showMenu();
}

void cancelTransaction() {

  coinStop();

  stopAllMotors();

  selectedProduct = -1;

  displayedCoins = getCoins();
  currentCoins = displayedCoins;

  state = CANCEL_NOTICE_STATE;

  stateStartMs = millis();

  showCancelBalance();
}

void beginDispensing() {

  if (selectedProduct < 0 || selectedProduct > 3) {
    return;
  }

  if (getCoins() < prices[selectedProduct]) {
    return;
  }

  coinStop();

  state = DISPENSING;

  stateStartMs = millis();

  motorForward((uint8_t)selectedProduct);

  showDispensing();
}

// =====================================================
// 14. TASK SENSOR - HC-SR04
// =====================================================

void TaskSensor(void *pvParameters) {

  (void)pvParameters;

  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {

    digitalWrite(TRIG_PIN, LOW);

    delayMicroseconds(3);

    digitalWrite(TRIG_PIN, HIGH);

    delayMicroseconds(10);

    digitalWrite(TRIG_PIN, LOW);

    unsigned long duration = pulseIn(
      ECHO_PIN,
      HIGH,
      25000UL
    );

    float measured = 999.0f;

    if (duration > 0) {

      measured = duration * 0.0343f / 2.0f;
    }

    distanceCm = measured;

    vTaskDelayUntil(
      &lastWake,
      pdMS_TO_TICKS(100)
    );
  }
}

// =====================================================
// 15. TASK FSM
// KEYPAD IS SCANNED DIRECTLY HERE
// NO KEYPAD TASK - NO QUEUE
// =====================================================

void TaskFSM(void *pvParameters) {

  (void)pvParameters;

  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {

    // -----------------------------------------------
    // A. SCAN KEYPAD
    // -----------------------------------------------

    char key = keypad.getKey();

    // Ignore keys during initial startup.

    if ((int32_t)(millis() - ignoreKeysUntil) < 0) {

      key = NO_KEY;
    }

    // -----------------------------------------------
    // B. UPDATE COIN BALANCE
    // -----------------------------------------------

    currentCoins = getCoins();

    if (state == INSERT_COINS &&
        currentCoins != displayedCoins) {

      displayedCoins = currentCoins;

      showPayment(currentCoins);
    }

    // -----------------------------------------------
    // C. FINITE STATE MACHINE
    // -----------------------------------------------

    switch (state) {

      // =============================================
      // STATE 1: SELECT PRODUCT
      // =============================================

      case SELECT_PRODUCT:

        if (key >= '1' && key <= '4') {

          selectedProduct = key - '1';

          showProduct();

          state = CONFIRM_PRODUCT;
        }

        break;

      // =============================================
      // STATE 2: CONFIRM PRODUCT
      // =============================================

      case CONFIRM_PRODUCT:

        if (key >= '1' && key <= '4') {

          selectedProduct = key - '1';

          showProduct();
        }

        else if (key == 'B') {

          state = INSERT_COINS;

          coinStart();

          currentCoins = getCoins();

          displayedCoins = currentCoins;

          showPayment(currentCoins);
        }

        else if (key == 'C') {

          cancelTransaction();
        }

        break;

      // =============================================
      // STATE 3: INSERT COINS
      // =============================================

      case INSERT_COINS:

        if (key == '#') {

          currentCoins = getCoins();

          if (selectedProduct >= 0 &&
              currentCoins >= prices[selectedProduct]) {

            beginDispensing();

          } else {

            lcd.setCursor(0, 2);

            lcd.print("CHUA DU XU!          ");
          }
        }

        else if (key == 'C') {

          cancelTransaction();
        }

        break;

      // =============================================
      // STATE 4: DISPENSING
      // =============================================

      case DISPENSING:

        // Product detected.

        if (getDistance() <= ITEM_PRESENT_CM) {

          motorStop((uint8_t)selectedProduct);

          state = WAIT_PICKUP;

          stateStartMs = millis();

          showWaitingPickup();
        }

        // Product delivery timeout.

        else if ((uint32_t)(millis() - stateStartMs)
                 >= PRODUCT_TIMEOUT_MS) {

          stopAllMotors();

          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("LOI XUAT SAN PHAM");

          lcd.setCursor(0, 1);
          lcd.print("Khong thay san pham");

          lcd.setCursor(0, 2);
          lcd.print("Kiem tra dong co");

          state = ERROR_STATE;

          stateStartMs = millis();
        }

        break;

      // =============================================
      // STATE 5: WAIT FOR CUSTOMER PICKUP
      // =============================================

      case WAIT_PICKUP:

        // Product removed.

        if (getDistance() >= ITEM_REMOVED_CM) {

          coinSubtract(prices[selectedProduct]);

          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("GIAO HANG HOAN TAT");

          lcd.setCursor(0, 1);
          lcd.print("Cam on quy khach!");

          lcd.setCursor(0, 2);
          lcd.print("So du con: ");
          lcd.print(getCoins());

          lcd.setCursor(0, 3);
          lcd.print("xu");

          state = COMPLETE_STATE;

          stateStartMs = millis();
        }

        // Pickup timeout: preserve coin balance.

        else if ((uint32_t)(millis() - stateStartMs)
                 >= REMOVE_TIMEOUT_MS) {

          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("SAN PHAM CHUA LAY");

          lcd.setCursor(0, 1);
          lcd.print("Chua tru so du xu");

          lcd.setCursor(0, 2);
          lcd.print("Bao luu: ");
          lcd.print(getCoins());

          lcd.setCursor(0, 3);
          lcd.print("xu");

          state = COMPLETE_STATE;

          stateStartMs = millis();
        }

        break;

      // =============================================
      // STATE 6: ERROR
      // =============================================

      case ERROR_STATE:

        if ((uint32_t)(millis() - stateStartMs)
            >= 2500UL) {

          resetTransaction();
        }

        break;

      // =============================================
      // STATE 7: COMPLETE
      // =============================================

      case COMPLETE_STATE:

        if ((uint32_t)(millis() - stateStartMs)
            >= 2500UL) {

          resetTransaction();
        }

        break;

      // =============================================
      // STATE 8: CANCEL NOTICE
      // =============================================

      case CANCEL_NOTICE_STATE:

        if ((uint32_t)(millis() - stateStartMs)
            >= 2500UL) {

          state = SELECT_PRODUCT;

          showMenu();
        }

        break;
    }

    // -----------------------------------------------
    // D. FSM SCAN PERIOD: 10 ms
    // -----------------------------------------------

    vTaskDelayUntil(
      &lastWake,
      pdMS_TO_TICKS(10)
    );
  }
}

// =====================================================
// 16. SETUP
// =====================================================

void setup() {

  // -----------------------------------------------
  // A. DISABLE JTAG, KEEP SWD
  // -----------------------------------------------

  #if defined(AFIO)
    AFIO->MAPR =
      (AFIO->MAPR & ~(7UL << 24)) |
      (2UL << 24);
  #endif

  // -----------------------------------------------
  // B. MOTOR INITIALIZATION
  // ALL MOTORS OFF AT STARTUP
  // -----------------------------------------------

  for (uint8_t i = 0; i < 4; i++) {

    pinMode(motorPins[i][0], OUTPUT);

    pinMode(motorPins[i][1], OUTPUT);

    digitalWrite(motorPins[i][0], LOW);

    digitalWrite(motorPins[i][1], LOW);
  }

  stopAllMotors();

  // -----------------------------------------------
  // C. LCD I2C INITIALIZATION
  // -----------------------------------------------

  Wire.setSDA(PB9);

  Wire.setSCL(PB8);

  Wire.begin();

  lcd.init();

  lcd.backlight();

  // -----------------------------------------------
  // D. KEYPAD INITIALIZATION
  // -----------------------------------------------

  keypad.setDebounceTime(40);

  keypad.setHoldTime(500);

  // -----------------------------------------------
  // E. COIN SENSOR INITIALIZATION
  // -----------------------------------------------

  pinMode(COIN_PIN, INPUT_PULLUP);

  coinAcceptEnabled = false;

  coinClear();

  attachInterrupt(
    digitalPinToInterrupt(COIN_PIN),
    coinSensorISR,
    FALLING
  );

  // -----------------------------------------------
  // F. HC-SR04 INITIALIZATION
  // -----------------------------------------------

  pinMode(TRIG_PIN, OUTPUT);

  digitalWrite(TRIG_PIN, LOW);

  pinMode(ECHO_PIN, INPUT);

  // -----------------------------------------------
  // G. INITIALIZE FSM
  // -----------------------------------------------

  state = SELECT_PRODUCT;

  selectedProduct = -1;

  currentCoins = 0;

  displayedCoins = 0;

  showMenu();

  ignoreKeysUntil = millis() + 1500UL;

  // -----------------------------------------------
  // H. CREATE FREERTOS TASKS
  // -----------------------------------------------

  BaseType_t okSensor = xTaskCreate(
    TaskSensor,
    "Sensor",
    256,
    nullptr,
    PRIORITY_SENSOR,
    nullptr
  );

  BaseType_t okFSM = xTaskCreate(
    TaskFSM,
    "FSM",
    512,
    nullptr,
    PRIORITY_FSM,
    nullptr
  );

  // -----------------------------------------------
  // I. CHECK TASK CREATION
  // -----------------------------------------------

  if (okSensor != pdPASS || okFSM != pdPASS) {

    stopAllMotors();

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print("TASK CREATE ERROR");

    while (1) {

      stopAllMotors();
    }
  }

  // -----------------------------------------------
  // J. START FREERTOS SCHEDULER
  // -----------------------------------------------

  vTaskStartScheduler();

  // If scheduler fails to start.

  stopAllMotors();

  lcd.clear();

  lcd.print("SCHEDULER ERROR");

  while (1) {

    stopAllMotors();
  }
}

// =====================================================
// 17. LOOP
// FREERTOS CONTROLS EXECUTION
// =====================================================

void loop() {

  // Do not execute application logic here.
  // All application tasks run under FreeRTOS.
}
