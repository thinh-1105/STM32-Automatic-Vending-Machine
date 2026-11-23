//Khung mã nguồn STM32 FreeRTOS
//A.1. Khai báo trạng thái FSM và thông số cảm biến
// HC-SR04: TRIG = PB7, ECHO = PB6
const uint8_t TRIG_PIN = PB7;
const uint8_t ECHO_PIN = PB6;
const float ITEM_PRESENT_CM = 8.0f;
const float ITEM_REMOVED_CM = 12.0f;
const uint32_t PRODUCT_TIMEOUT_MS = 5000UL;
const uint32_t REMOVE_TIMEOUT_MS = 30000UL;
volatile float distanceCm = 999.0f;
// FSM
enum MachineState {
  SELECT_PRODUCT,
  CONFIRM_PRODUCT,
  INSERT_COINS,
  DISPENSING,
  WAIT_PICKUP,
  ERROR_STATE,
  COMPLETE_STATE,
  CANCEL_NOTICE_STATE  };
MachineState state = SELECT_PRODUCT;
int selectedProduct = -1;
uint16_t displayedCoins = 0;
uint16_t currentCoins = 0;
uint32_t stateStartMs = 0;
uint32_t ignoreKeysUntil = 0;
//A.2. Đọc khoảng cách cảm biến HC-SR04 có timeout
void TaskSensor(void *pvParameters) {
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(3);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    unsigned long duration =
        pulseIn(ECHO_PIN, HIGH, 25000UL);
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
//A.3. Xử lý trạng thái xuất sản phẩm và timeout
case DISPENSING:
  if (getDistance() <= ITEM_PRESENT_CM) {
    motorStop((uint8_t)selectedProduct);
    state = WAIT_PICKUP;
    stateStartMs = millis();
    showWaitingPickup();
  }
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
//A.4. Xử lý người dùng lấy sản phẩm
case WAIT_PICKUP:
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
//A.5. Xử lý lỗi và hoàn tất giao dịch
case ERROR_STATE:
  if ((uint32_t)(millis() - stateStartMs)
      >= 2500UL) {
    resetTransaction();
  }
  break;
case COMPLETE_STATE:
  if ((uint32_t)(millis() - stateStartMs)
      >= 2500UL) {
    resetTransaction();
  }

  break;
case CANCEL_NOTICE_STATE:
  if ((uint32_t)(millis() - stateStartMs)
      >= 2500UL) {
    state = SELECT_PRODUCT;
    showMenu();
  }
  break;
//A.6. Khởi tạo tác vụ FreeRTOS
keypadQueue = xQueueCreate(8, sizeof(char));
BaseType_t ok1 = xTaskCreate(
    TaskKeypad, "Keypad", 160,
    nullptr, PRIORITY_KEYPAD, nullptr
);
BaseType_t ok2 = xTaskCreate(
    TaskSensor, "Sensor", 192,
    nullptr, PRIORITY_SENSOR, nullptr
);
BaseType_t ok3 = xTaskCreate(
    TaskFSM, "FSM", 320,
    nullptr, PRIORITY_FSM, nullptr
);

if (ok1 != pdPASS ||
    ok2 != pdPASS ||
    ok3 != pdPASS) {
  lcd.clear();
  lcd.print("TASK CREATE ERROR");
  while (1) {
    stopAllMotors();
  }
}
vTaskStartScheduler();

