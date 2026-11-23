//Mã nguồn chương trình điều khiển thực tế của nhóm 

//Task 1 – Đọc bàn phím và gửi dữ liệu vào Queue
void TaskKeypad(void *pvParameters) {
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    char key = keypad.getKey();
    if (key != NO_KEY) {
      xQueueSend(keypadQueue, &key, 0);
    }
    vTaskDelayUntil(
      &lastWake,
      pdMS_TO_TICKS(10)
    );
  }
}
//Task 2 – Đọc cảm biến HC-SR04 có timeout
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
//Task 3 – Điều khiển máy trạng thái FSM
void TaskFSM(void *pvParameters) {
  (void)pvParameters;
  TickType_t lastWake = xTaskGetTickCount();
  char key = NO_KEY;
  for (;;) {
    key = NO_KEY;
    xQueueReceive(keypadQueue, &key, 0);
    if ((int32_t)(millis() - ignoreKeysUntil) < 0) {
      key = NO_KEY;
    }
    currentCoins = getCoins();
    if (state == INSERT_COINS && currentCoins != displayedCoins) {
      displayedCoins = currentCoins;
      showPayment(currentCoins);
    }
    switch (state) {
      case SELECT_PRODUCT:
        if (key >= '1' && key <= '4') {
          selectedProduct = key - '1';
          showProduct();
          state = CONFIRM_PRODUCT;
        }
        break;
      case CONFIRM_PRODUCT:
        if (key >= '1' && key <= '4') {
          selectedProduct = key - '1';
          showProduct();
        } else if (key == 'B') {
          state = INSERT_COINS;
          coinStart();
          currentCoins = getCoins();
          displayedCoins = currentCoins;
          showPayment(currentCoins);
        } else if (key == 'C') {
          cancelTransaction();
        }
        break;
      case INSERT_COINS:
        if (key == '#') {
          currentCoins = getCoins();
          if (selectedProduct >= 0 && currentCoins >= prices[selectedProduct]) {
            beginDispensing();
          } else {
            lcd.setCursor(0, 2);
            lcd.print("CHUA DU XU!          ");
          }
        } else if (key == 'C') {
          cancelTransaction();
        }
        break;
      case DISPENSING:
        if (getDistance() <= ITEM_PRESENT_CM) {
          motorStop((uint8_t)selectedProduct);
          state = WAIT_PICKUP;
          stateStartMs = millis();
          showWaitingPickup();
        } else if ((uint32_t)(millis() - stateStartMs) >= PRODUCT_TIMEOUT_MS) {
          stopAllMotors();
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("LOI XUAT SAN PHAM");
          lcd.setCursor(0, 1); lcd.print("Khong thay san pham");
          lcd.setCursor(0, 2); lcd.print("Kiem tra dong co");
          state = ERROR_STATE;
          stateStartMs = millis();
        }
        break;
      case WAIT_PICKUP:
        if (getDistance() >= ITEM_REMOVED_CM) {
          coinSubtract(prices[selectedProduct]);
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("GIAO HANG HOAN TAT");
          lcd.setCursor(0, 1); lcd.print("Cam on quy khach!");
          lcd.setCursor(0, 2); lcd.print("So du con: "); lcd.print(getCoins());
          lcd.setCursor(0, 3); lcd.print("xu");
          state = COMPLETE_STATE;
          stateStartMs = millis();
        } else if ((uint32_t)(millis() - stateStartMs) >= REMOVE_TIMEOUT_MS) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("SAN PHAM CHUA LAY");
          lcd.setCursor(0, 1); lcd.print("Chua tru so du xu");
          lcd.setCursor(0, 2); lcd.print("Bao luu: "); lcd.print(getCoins());
          lcd.setCursor(0, 3); lcd.print("xu");
          state = COMPLETE_STATE;
          stateStartMs = millis();
        }
        break;
      case ERROR_STATE:
        if ((uint32_t)(millis() - stateStartMs) >= 2500UL) {
          resetTransaction();
        }
        break;
      case COMPLETE_STATE:
        if ((uint32_t)(millis() - stateStartMs) >= 2500UL) {
          resetTransaction();
        }
        break;
      case CANCEL_NOTICE_STATE:
        if ((uint32_t)(millis() - stateStartMs) >= 2500UL) {
          state = SELECT_PRODUCT;
          showMenu();
        }
        break;
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
  }
}
//Task 4. Khởi tạo Queue và tạo các Task
keypadQueue = xQueueCreate(8, sizeof(char));
if (keypadQueue == nullptr) {
  lcd.clear();
  lcd.print("QUEUE CREATE ERROR");

  while (1) {
    stopAllMotors();
  }
}
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
