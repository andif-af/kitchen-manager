#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>

#define INCLUDE_vTaskSuspend 1

/* HC-SR04 Pins */
#define TRIGGER_PIN 13
#define ECHO_PIN 14

/* Button pin */
#define BUTTON_PIN 4

/* WiFi */
const char* ssid = "Ujajja";
const char* pass = "tidaktahu";

/* BLE */
const char* sv_uuid = "e8f3bfac-6119-42b0-9a00-e9e8057e42b1";
const char* ch_uuid = "9c9aa4b2-ba8b-41cf-affc-423ff60c8f2e";
BLEServer* pServer;
BLEService* pService;
BLECharacteristic* pQueueChar;
BLECharacteristic* pCancelChar;
BLEAdvertising* pAd;

/* FreeRTOS handles */
TimerHandle_t xInactivityWatchdog = NULL;
TaskHandle_t xQueueManagerHandle, xBLECommsHandle, xMonitorHandle;
SemaphoreHandle_t xSendingBlocker, xReceivingBlocker, xMonitorBlocker;
QueueHandle_t xKitchenQueue;

/* Global variables */
bool activity = false;

/* BLE Callbacks */

class TheCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCh){

  }

  void onWrite(BLECharacteristic* pCh){

    if (pCh == pQueueChar){
      xSemaphoreGiveFromISR(xSendingBlocker, 0);
    }

  }
};

/* FreeRTOS Callbacks */

void vWatchdogCallback(TimerHandle_t watchdog){
  activity = false;
  xSemaphoreGiveFromISR(xReceivingBlocker, NULL);
}

/* Button Press Callback*/

void onButtonPress(){
  activity = false;
  xSemaphoreGiveFromISR(xReceivingBlocker, NULL);
}

/* FreeRTOS Tasks */

void vQueueManager (void* params){

  byte received;

  while(1){
        
    /* Check if queue is empty or not */

    if(xQueueReceive(xKitchenQueue, (void *) &received, 10) == pdTRUE){
      
      /* Start the timer and monitor activities */
      xTimerStart(xInactivityWatchdog, 0);
      activity = true;

      /*Block this task until watchdog times out or after button press */      
      xSemaphoreTake(xReceivingBlocker, portMAX_DELAY);

    }else {

      vTaskDelay(1000 / portTICK_PERIOD_MS);

    }

    /**/

  }

}

void vBLEComms (void* params){

  String var;
  int current_length;

  while(1){
    /* Make this block until BLE characteristics are written */
    xSemaphoreTake(xSendingBlocker, portMAX_DELAY);
    
    /* When the characteristic has been written from end devices */
    Serial.println("Writes detected, processing...");
    current_length = uxQueueMessagesWaiting(xKitchenQueue);

    var = pQueueChar->getValue().c_str();

    if (xQueueSend(xKitchenQueue, (void*) &var[current_length], 0) == pdTRUE){
      Serial.println("Sending to queue succeed!");
    } else {
      Serial.println("Sending to queue failed... ._.)");
    }
    
  }

}

void vActivityMonitor (void* params){

  while(1){
    
    if (activity){

      /*
      For 200cm distance,
      200 = t * 0.034 / 2
      t = 200 * 2 / 0.034 
        = 11764.706 ms
      */

      if (sendPulse() <= 11765){
        xTimerReset(xInactivityWatchdog, 0);
      }

    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);

  }

}

/*Additional functions*/

long sendPulse (){

  long duration;

  /*Triggering pulse generation*/
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH);
  return duration;

}

void setup() {

  /* pinMode section */

  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  /* BLE section */
  
  BLEDevice::init("BLE_Server");
  pServer = BLEDevice::createServer();
  pService = pServer->createService(sv_uuid);

  /* Create a characteristic for managing queues */
  pQueueChar = pService->createCharacteristic(ch_uuid, 
                                         BLECharacteristic::PROPERTY_READ | 
                                         BLECharacteristic::PROPERTY_WRITE);

  

  /*
  Queues will be represented by an array of 4 bytes. Maximum queue length
  of 4. Each byte identifies the next person to use the kitchen.
  */

  pQueueChar->setValue(0x00000000);

  /* Create another characteristic for managing queue cancellations */

  pCancelChar = pService->createCharacteristic(ch_uuid, 
                                         BLECharacteristic::PROPERTY_READ | 
                                         BLECharacteristic::PROPERTY_WRITE);

  /* This will represent which user who wants to cancel their booking */

  pCancelChar->setValue(0x00000000);

  /* Start advertisements */

  pAd = BLEDevice::getAdvertising();
  pAd->addServiceUUID(sv_uuid);
  pAd->setScanResponse(true);

  /* Timer Creation */

  xInactivityWatchdog = xTimerCreate("Inactivity Watchdog",
                                    300000 / portTICK_PERIOD_MS,
                                    pdFALSE,
                                    (void*) 0,
                                    vWatchdogCallback);

  /* Attach button interrupt */

  attachInterrupt(BUTTON_PIN, onButtonPress, FALLING);

}

void loop() {
  // put your main code here, to run repeatedly:
}

