#include <Arduino.h>
#include <cstdio>
#include <cinttypes>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_timer.h"
#include <iostream>
#include "PreferencesWrapper.h"
#include <cstdio>
#include "WS2812BLedMatrix.h"
#include <vector>
#include <algorithm>
#include <map>
#include <WiFi.h>
#include "SettingsManager.h"
#include "esp_task_wdt.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "WebTerminal.h"
#include "terminal.html.h"
#include <ElegantOTA.h>
#include "resitancemeasurement.h"
#include "freertos/task.h"  // Required for vTaskList()
using namespace std;

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h" // Required for gpio_pad_select_gpio()
#include "soc/io_mux_reg.h" // For IO_MUX register definitions
#include "esp_log.h"
#include "GpioHoldManager.h"

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex
#define WAKEUP_GPIO_32              GPIO_NUM_32     // Only RTC IO are allowed - ESP32 Pin example
#define WAKEUP_GPIO_34              GPIO_NUM_34     // Only RTC IO are allowed - ESP32 Pin example
#define WAKEUP_GPIO_35              GPIO_NUM_35     // Only RTC IO are allowed - ESP32 Pin example
#define WAKEUP_GPIO_36              GPIO_NUM_36     // Only RTC IO are allowed - ESP32 Pin example
#define WAKEUP_GPIO_39              GPIO_NUM_39     // Only RTC IO are allowed - ESP32 Pin example
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60*1       /* Time ESP32 will go to sleep (in seconds) */

// Define bitmask for multiple GPIOs
uint64_t bitmask = BUTTON_PIN_BITMASK(WAKEUP_GPIO_32) | BUTTON_PIN_BITMASK(WAKEUP_GPIO_34) | BUTTON_PIN_BITMASK(WAKEUP_GPIO_35) | BUTTON_PIN_BITMASK(WAKEUP_GPIO_36) | BUTTON_PIN_BITMASK(WAKEUP_GPIO_39);

TaskHandle_t TesterTask;
GpioHoldManager holdManager;
void prepareforDeepSleep()
{

  //Use ext1 as a wake-up source
  esp_sleep_enable_ext1_wakeup(bitmask, ESP_EXT1_WAKEUP_ANY_HIGH);
  //esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  //Waarschijnlijk moet ik van de ADC pinnen eerst nog gewone IO pinnen maken
  pinMode(WAKEUP_GPIO_32, INPUT);  // br
  pinMode(WAKEUP_GPIO_35, INPUT);  // cr
  pinMode(WAKEUP_GPIO_36, INPUT);  // cl
  pinMode(WAKEUP_GPIO_39, INPUT);  // bl
  pinMode(WAKEUP_GPIO_34, INPUT);  // piste

  
  pinMode(al_driver, OUTPUT);
  pinMode(bl_driver, OUTPUT);
  pinMode(cl_driver, OUTPUT);
  //pinMode(ar_driver, OUTPUT);
  pinMode(br_driver, OUTPUT);
  pinMode(cr_driver, OUTPUT);
  pinMode(piste_driver, OUTPUT);
  pinMode(PIN, OUTPUT);

  digitalWrite(al_driver, 1);
  digitalWrite(bl_driver, 0);
  digitalWrite(cl_driver, 0);
  //digitalWrite(ar_driver, 0);
  digitalWrite(br_driver, 0);
  digitalWrite(cr_driver, 0);
  digitalWrite(piste_driver, 0);
  digitalWrite(PIN, 0);

  holdManager.enableAll();
  vTaskDelay(300 / portTICK_PERIOD_MS);
  esp_task_wdt_deinit(); // <--- Add this line to disable the task WDT

  Serial.println("Entering deep sleep...");
  Serial.flush();
  esp_deep_sleep_start();
}
void returnFromDeepSleep() {
  // This function is called when the ESP32 wakes up from deep sleep
  Serial.println("Waking up from deep sleep...");
  holdManager.disableAll();
}



void printTasks() {
    // Get task list buffer
    char *taskListBuffer = (char *)pvPortMalloc(2048);  // Adjust size as needed
    if(!taskListBuffer) {
        ESP_LOGE("TASKS", "Failed to allocate task list buffer");
        return;
    }

    // Get raw task list
    vTaskList(taskListBuffer);
    
    // Parse and enhance with core info
    printf("\nTask Name      | Core | State | Pri | Stack Free |\n");
    printf("---------------------------------------------------\n");
    
    char *line = strtok(taskListBuffer, "\n");
    while(line != NULL) {
        char taskName[32];
        char state;
        unsigned int priority;
        unsigned int stackFree;
        unsigned int taskNumber;

        // Parse vTaskList format: "TaskName S R 1 1234 5"
        sscanf(line, "%31s %c %*c %u %u %u", 
              taskName, &state, &priority, &stackFree, &taskNumber);

        // Get task handle from task number (ESP32-specific)
        TaskHandle_t handle = xTaskGetHandle(taskName);
        
        // Get core affinity
        BaseType_t core = -1;
        if(handle) {
            BaseType_t affinity = xTaskGetAffinity(handle);
            if(affinity == 1) core = 0;
            else if(affinity == 2) core = 1;
        }

        // Print enhanced line
        printf("%-14s | %-3s | %-5c | %-3u | %-10u |\n",
              taskName,
              (core >= 0) ? (core == 0 ? "0" : "1") : "?",
              state,
              priority,
              stackFree);

        line = strtok(NULL, "\n");
    }
    
    vPortFree(taskListBuffer);
}



enum State_t {Waiting, WireTesting_1,WireTesting_2, FoilTesting, EpeeTesting};


#define FOIL_TEST_TIMEOUT 12
#define WIRE_TEST_1_TIMEOUT 2
#define NO_WIRES_PLUGGED_IN_TIMEOUT 2



long TimeToDeepSleep=-1;
long IdleTimeToSleep = 3000000;

int myRefs_Ohm[]={0,1,2,3,4,5,6,7,8,9,10};
int StoredRefs_ohm[] = {0,1,2,3,4,5,6,7,8,9,10};
int R0 = 130;
int Vmax = 2992;
volatile bool DoCalibration = false;

AsyncWebServer server(80);
SettingsManager settings;
WebTerminal terminal(server);

/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}




WS2812B_LedMatrix *LedPanel;
#define MY_ATTENUATION  ADC_ATTEN_DB_11
//#define MY_ATTENUATION  ADC_ATTEN_DB_6

bool delayAndTestWirePluggedIn( long delay){
long returntime = millis() + delay;
  while(millis() < returntime){
    esp_task_wdt_reset();
    testWiresOnByOne();
    if(WirePluggedIn())
      return true;
  }
  return false;

}

bool delayAndTestWirePluggedInFoil( long delay){
long returntime = millis() + delay;
  while(millis() < returntime){
    esp_task_wdt_reset();
    testWiresOnByOne();  
  }
  return false;

}


void DoFoilTest() {
  int timeout = FOIL_TEST_TIMEOUT;
  testWiresOnByOne();
  while(!WirePluggedIn()){
  esp_task_wdt_reset();
  LedPanel->Draw_F(LedPanel->m_White);
  while((testArBr()<myRefs_Ohm[4]));
  // Check if valid or non-valid test
  if(testArCl()<myRefs_Ohm[5])
    LedPanel->SetFullMatrix(LedPanel->m_Green);
  else
   LedPanel->SetFullMatrix(LedPanel->m_White);
  esp_task_wdt_reset();
  if(delayAndTestWirePluggedInFoil(1000))
    break;
  esp_task_wdt_reset();
  if(testArBr()<myRefs_Ohm[4]){
    LedPanel->ClearAll();
    LedPanel->Draw_F(LedPanel->m_White);
    LedPanel->myShow();
    timeout = FOIL_TEST_TIMEOUT;
  }
  testWiresOnByOne();
  }
  LedPanel->ClearAll();
  LedPanel->myShow();
}

void DoLameTest() {
  bool bShowingRed = false;
  testWiresOnByOne();
  while(!WirePluggedIn()){
  esp_task_wdt_reset();
  if(testBrCr()< myRefs_Ohm[5]){
    LedPanel->DrawDiamond(LedPanel->m_Green);
    bShowingRed = false;
    while((testBrCr()<myRefs_Ohm[5])){esp_task_wdt_reset();};
  }
  else{
    if(testBrCr()< myRefs_Ohm[10]){
    LedPanel->DrawDiamond(LedPanel->m_Yellow);
    bShowingRed = false;
    while((testBrCr()<myRefs_Ohm[10])){esp_task_wdt_reset();};
    }
  }
  
  if(!bShowingRed)
    LedPanel->DrawDiamond(LedPanel->m_Red);
  bShowingRed = true;
  esp_task_wdt_reset();
 
  if(delayAndTestWirePluggedIn(400))
    break;
  esp_task_wdt_reset();
  testWiresOnByOne();
  }
  LedPanel->ClearAll();
  LedPanel->myShow();
}

void DoEpeeTest() {
  Serial.println("In Epee testing");
  bool bArCr = false;
  bool bArBr = false;
  bool bBrCr = false;
  testWiresOnByOne();
  while(!WirePluggedIn()){
    bArCr = (testArCr()<myRefs_Ohm[4]);
    bArBr = (testArBr()<myRefs_Ohm[10]);
    bBrCr = (testBrCr()<myRefs_Ohm[10]);
    esp_task_wdt_reset();
    if(bArCr && !bArBr && !bBrCr){
      LedPanel->SetFullMatrix(LedPanel->m_Green);
      if(delayAndTestWirePluggedIn(1000))
        break;
      esp_task_wdt_reset();
      if(testArCr()>myRefs_Ohm[4]){
        LedPanel->ClearAll();
        LedPanel->myShow();
      }
    }
    if(bArBr){
      LedPanel->ClearAll();
      LedPanel->AnimateArBrConnection();
    }
    if(bBrCr){
      LedPanel->ClearAll();
      LedPanel->AnimateBrCrConnection();
    }
    if(!(bArCr || bArBr || bBrCr)){
      LedPanel->Draw_E(LedPanel->m_Green);
    }
    esp_task_wdt_reset();
    testWiresOnByOne();
  }
  LedPanel->ClearAll();
  LedPanel->myShow();

}



bool AnimateSingleWire(int i)
{
  bool bOK= false;
    if(measurements[i][i] < myRefs_Ohm[10])
    {
        if((measurements[i][(i+1)%3]> 200 ) && (measurements[i][(i+2)%3] > 200))
        {
          // OK
          int level = 2;
          if(measurements[i][i] <= myRefs_Ohm[3])
          level = 1;
          if(measurements[i][i] <= myRefs_Ohm[1])
          level  = 0;


          LedPanel->AnimateGoodConnection(i, level);
          bOK= true;
        }
        else
        {
            // short
            if(measurements[i][(i+1)%3]< 160)
              LedPanel->AnimateShort(i, (i+1)%3);
            else
            if(measurements[i][(i+2)%3] < 160)
              LedPanel->AnimateShort(i, (i+2)%3);
        }
    }
    else
    {
        if((measurements[i][(i+1)%3]>160) && (measurements[i][(i+2)%3]>160))
        {
            // Simply broken
            LedPanel->AnimateBrokenConnection(i);
        }
        else
        {
          if(measurements[i][(i+1)%3]<160)
            LedPanel->AnimateWrongConnection(i,(i+1)%3);
          if(measurements[i][(i+2)%3] < 160)
            LedPanel->AnimateWrongConnection(i,(i+2)%3);
        }
    }
  return bOK;
}

bool DoQuickCheck(){
bool bAllGood = true;
  testWiresOnByOne();
  for(int i = 0; i < 3; i++)
  {
    bAllGood &= AnimateSingleWire(i);
  }
  esp_task_wdt_reset();
  vTaskDelay(500 / portTICK_PERIOD_MS);
  esp_task_wdt_reset();
  LedPanel->ClearAll();
  esp_task_wdt_reset();
  vTaskDelay(300 / portTICK_PERIOD_MS);
  esp_task_wdt_reset();
  return bAllGood;
}


void handleCalibrateCommand(const std::vector<String>& args) {
    if (args.empty()) {
        terminal.printf("Calibrate command requires 'on' or 'off'\n");
        return;
    }

    if (args[0] == "on") {
        DoCalibration = true;
        
    } else if (args[0] == "off") {
        DoCalibration = false;
        
    } else {
        terminal.printf("Unknown argument for Calibrate command\n");
    }
}

void AdjustThreasholdForRealV() {
  static long TimeToTest = 0;
 int Vreal = 3300;
 bool OKToAdjust = true;
 testWiresOnByOne();
 if(millis() < TimeToTest)
    return;
 for(int j=0;j<3;j++){
      if((measurements[j][j] > 2500)){
        if(measurements[j][j] < Vreal)
          Vreal = measurements[j][j];
      }
      else{
        OKToAdjust = false;
        j = 3;
      }
        
    } 
  if(OKToAdjust){
    
    float factor = (float)Vreal / (float)Vmax;
    for(int i = 0; i< 11; i++){
      myRefs_Ohm[i] = (int)(StoredRefs_ohm[i]*factor);
    }
  }
  TimeToTest = millis() + 5000;
}

#define NR_SAMPLES_DIVIDER 10
#define NR_SAMPLES  (1L << NR_SAMPLES_DIVIDER)

// Calculate the best representative value for ADC measurements
long calculateBestADCValue(const std::vector<long>& sortedSamples) {
  if(sortedSamples.empty()) return 0;
  
  // Create frequency map
  std::map<long, int> valueCount;
  for(const auto& val : sortedSamples) {
    valueCount[val]++;
  }
  
  // Method 1: Weighted average of most frequent triplet (best for stable measurements)
  long bestTripletCenter = sortedSamples[sortedSamples.size()/2]; // fallback to median
  int bestTripletSum = 0;
  
  for(const auto& pair : valueCount) {
    long centerValue = pair.first;
    int tripletSum = 0;
    
    // Sum frequencies of center-1, center, center+1
    auto it = valueCount.find(centerValue - 1);
    if(it != valueCount.end()) tripletSum += it->second;
    
    tripletSum += pair.second; // center value
    it = valueCount.find(centerValue + 1);
    if(it != valueCount.end()) tripletSum += it->second;
    
    if(tripletSum > bestTripletSum) {
      bestTripletSum = tripletSum;
      bestTripletCenter = centerValue;
    }
  }
  
  // Calculate weighted average of the triplet
  long weightedSum = 0;
  int totalWeight = 0;
  
  for(long val = bestTripletCenter - 1; val <= bestTripletCenter + 1; val++) {
    auto it = valueCount.find(val);
    if(it != valueCount.end()) {
      weightedSum += val * it->second;
      totalWeight += it->second;
    }
  }
  
  return (totalWeight > 0) ? (weightedSum / totalWeight) : bestTripletCenter;
}

// Alternative: Trimmed mean (good for general noise reduction)
long calculateTrimmedMean(const std::vector<long>& sortedSamples, float trimPercent = 0.1f) {
  if(sortedSamples.empty()) return 0;
  
  int trimCount = (int)(sortedSamples.size() * trimPercent / 2);
  if(trimCount >= sortedSamples.size()/2) trimCount = 0;
  
  long sum = 0;
  int count = 0;
  
  for(int i = trimCount; i < sortedSamples.size() - trimCount; i++) {
    sum += sortedSamples[i];
    count++;
  }
  
  return (count > 0) ? (sum / count) : sortedSamples[sortedSamples.size()/2];
}

void Calibrate()
{

long value = 0;
//long values[NR_SAMPLES];
long sample;
vector<long> SortedSamples[3];

long max[] ={0,0,0};
long min[] = {9999,9999,9999};
long current_time;
long duration;

while(DoCalibration){
value = 0;

esp_task_wdt_reset();
SortedSamples[0].clear();
SortedSamples[1].clear();
SortedSamples[2].clear();
current_time = millis();
for(int i = 0; i< NR_SAMPLES; i++){
  testStraightOnly();
  esp_task_wdt_reset();
  for(int j=0;j<3;j++){
    auto it = std::lower_bound(SortedSamples[j].begin(), SortedSamples[j].end(), measurements[j][j]);
    SortedSamples[j].insert(it, measurements[j][j]);
  }

      
  }
   duration = (millis()-current_time);
      
 
      terminal.printf("Duration = %d\n",duration);
      
    for(int i = 0; i<3;i++){

      if(min[i] > SortedSamples[i][0])
        min[i] = (SortedSamples[i])[0];
      //terminal.printf("   Min[%d] = %d", i, min[i]);
      
      if(max[i] < SortedSamples[i][NR_SAMPLES-1] )
        max[i] = SortedSamples[i][NR_SAMPLES-1] ;
      
      //terminal.printf("   Max[%d] = %d", i, max[i]);
      int percentile_50_index = (int)((long)NR_SAMPLES * 50L) / 100;
      int percentile_90_index = (int)((long)NR_SAMPLES * 90L) / 100;
      
      // Find most common value
      std::map<long, int> valueCount;
      for(const auto& val : SortedSamples[i]) {
        valueCount[val]++;
      }
      
      long mostCommonValue = SortedSamples[i][0];
      int maxCount = 0;
      for(const auto& pair : valueCount) {
        if(pair.second > maxCount) {
          maxCount = pair.second;
          mostCommonValue = pair.first;
        }
      }
      
      // Find most frequent 3 consecutive values
      long bestTripletCenter = mostCommonValue;
      int bestTripletSum = 0;
      
      for(const auto& pair : valueCount) {
        long centerValue = pair.first;
        int tripletSum = 0;
        
        // Sum frequencies of center-1, center, center+1
        auto it = valueCount.find(centerValue - 1);
        if(it != valueCount.end()) tripletSum += it->second;
        
        tripletSum += pair.second; // center value
        
        it = valueCount.find(centerValue + 1);
        if(it != valueCount.end()) tripletSum += it->second;
        
        if(tripletSum > bestTripletSum) {
          bestTripletSum = tripletSum;
          bestTripletCenter = centerValue;
        }
      }
      
      // Find percentile of best triplet center
      int tripletPercentile = 0;
      for(int k = 0; k < SortedSamples[i].size(); k++) {
        if(SortedSamples[i][k] == bestTripletCenter) {
          tripletPercentile = (k * 100) / NR_SAMPLES;
          break;
        }
      }
      
      // Calculate best ADC values using different methods
      long bestADCValue = calculateBestADCValue(SortedSamples[i]);
      long trimmedMean = calculateTrimmedMean(SortedSamples[i], 0.1f); // Trim 10% from each end
      
      terminal.printf("   Min[%d] = %d      Max[%d] = %d      P50[%d] = %d      P90[%d] = %d\n", 
                      i, SortedSamples[i][0], i, SortedSamples[i][NR_SAMPLES-1], i, SortedSamples[i][percentile_50_index], i, SortedSamples[i][percentile_90_index]);
      terminal.printf("   Most common[%d] = %d (%dx)      Best triplet[%d] = %d (%dx) P%d\n", 
                      i, mostCommonValue, maxCount, i, bestTripletCenter, bestTripletSum, tripletPercentile);
      terminal.printf("   **BEST ADC[%d] = %d**      Trimmed mean[%d] = %d\n", 
                      i, bestADCValue, i, trimmedMean);
    //terminal.printf(".");
    }
}
      //av = 0;

    /*while(1){
      for (int i = 0; i < 512; i++){
        Set_IODirectionAndValue(IODirection_cr_cl,IOValues_cr_cl);
        delay(1);
        value = getSample(cr_analog);
        if(min > value)
          min = value;
        if(max < value)
          max = value;
        av +=value;
        
      }
      av>>=9;
      Serial.print("Av = ");
      Serial.print(av);
      Serial.print("   Min = ");
      Serial.print(min);
      Serial.print("   Max = ");
      Serial.println(max);
      av = 0;
    }*/


}

int timetoswitch = WIRE_TEST_1_TIMEOUT;
int NoWireTimeout = NO_WIRES_PLUGGED_IN_TIMEOUT;
bool bAllGood = true;
State_t State = Waiting;






void TesterHandler(void *parameter)
{
  while(true)
  {
  if(DoCalibration){
    Calibrate();
  }

  esp_task_wdt_reset();
  if(Waiting == State)
  {
      if(testArCr()<160){
        State = EpeeTesting;
        DoEpeeTest();
        State = Waiting;
        esp_task_wdt_reset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
        TimeToDeepSleep = millis()+IdleTimeToSleep;
      }
      else{
        if(testArBr()<160){
          //State = FoilTesting;
          DoFoilTest();
          State = Waiting;
          esp_task_wdt_reset();
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          esp_task_wdt_reset();
          TimeToDeepSleep = millis()+IdleTimeToSleep;
        }
        else{
          if(testBrCr()<160){
          //State = LameTesting;
          DoLameTest();
          State = Waiting;
          esp_task_wdt_reset();
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          esp_task_wdt_reset();
          TimeToDeepSleep = millis()+IdleTimeToSleep;
          }
        }
      }
      esp_task_wdt_reset();
      //testWiresOnByOne();
      AdjustThreasholdForRealV();
      if(WirePluggedIn()){
        State = WireTesting_1;
        NoWireTimeout = NO_WIRES_PLUGGED_IN_TIMEOUT;
        timetoswitch = WIRE_TEST_1_TIMEOUT;

      }
      esp_task_wdt_reset();
      if(IdleTimeToSleep < millis()){
        Serial.println("Going to sleep now");
        Serial.flush();
        prepareforDeepSleep();

      }
  }

  esp_task_wdt_reset();
  if(WireTesting_1 == State)
  {
    TimeToDeepSleep = millis()+IdleTimeToSleep;
    testWiresOnByOne();
    bAllGood = DoQuickCheck();
    if(bAllGood){
      timetoswitch--;
    }
    else{
      timetoswitch = WIRE_TEST_1_TIMEOUT;
      if(!WirePluggedIn())
        NoWireTimeout--;
      else {
        NoWireTimeout = NO_WIRES_PLUGGED_IN_TIMEOUT;
      }
      if(!NoWireTimeout)
        State = Waiting;

    }
  if(!timetoswitch){
    for(int i = 0; i< 5; i+=2){
      LedPanel->SetLine(i, LedPanel->m_Green);
    }
      LedPanel->myShow();
      esp_task_wdt_reset();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      esp_task_wdt_reset();
      State = WireTesting_2;
    }
  }
  if(WireTesting_2 == State)
  {
    TimeToDeepSleep = millis()+IdleTimeToSleep;
    for(int i= 100000;i>0;i--){
      esp_task_wdt_reset();
      if(!testStraightOnly())
        i = 0;
    }

    LedPanel->ClearAll();
    LedPanel->myShow();
    for(int i = 0; i < 3; i++)
    {
      bAllGood &= AnimateSingleWire(i);
    }
    esp_task_wdt_reset();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_task_wdt_reset();
    timetoswitch = 3;
    LedPanel->ClearAll();
    LedPanel->myShow();
    State = Waiting;
  }
    
  }
}

bool CalibrationEnabled;

String deviceName;

void SetupNetworkStuff(){
  Serial.println("Not returning from sleep");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Tester", "01041967");
    ElegantOTA.begin(&server);    // Start ElegantOTA
    Serial.println(WiFi.softAPIP());
    terminal.begin();
  // Register command handlers
  terminal.registerCommand("echo", [](const std::vector<String>& args) {
        String response;
        for (auto& arg : args) {
            response += arg + " ";
        }
        terminal.send(response);
    });

    terminal.registerCommand("reboot", [](const std::vector<String>&) {
        terminal.send("Rebooting...");
        ESP.restart();
    });
    terminal.registerCommand("calibrate", handleCalibrateCommand);


  server.begin();
  terminal.printf("HTTP server started\n");
  // Register settings
  settings.addBool("bCalibrate", "Perform Calibration?", &CalibrationEnabled);
  settings.addIntArray("myRefs_Ohm", "Threshold values from 0 - 10 Ohm", StoredRefs_ohm, 11);
  for(int i = 0; i< 11; i++){
      myRefs_Ohm[i]= StoredRefs_ohm[i];
  }
  settings.addInt("R0", "R0 (total resistance (Ron + 2 x 47)", &R0);
  settings.addInt("Vmax", "Vmax in mV", &Vmax);
  settings.addString("name", "Device Name", &deviceName);
  settings.begin("Settings");        // for Preferences namespace
  settings.load();
  settings.addWebEndpoints(server);  // to set up web routes

}
bool bReturnFromSleep = false;


void setup() {
  // put your setup code here, to run once:
  setCpuFrequencyMhz(240); // Set CPU frequency to 240 MHz
  // Register all pins you want to manage
    holdManager.add((gpio_num_t)al_driver);
    holdManager.add((gpio_num_t)bl_driver);
    holdManager.add((gpio_num_t)cl_driver);
    holdManager.add((gpio_num_t)br_driver);
    holdManager.add((gpio_num_t)cr_driver);
    holdManager.add((gpio_num_t)piste_driver);
    holdManager.add((gpio_num_t)PIN);  

  LedPanel = new WS2812B_LedMatrix();
  LedPanel->begin();
  LedPanel->ClearAll();
  Serial.begin(115200);
  esp_task_wdt_init(20, true);
  esp_task_wdt_add(NULL);
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  init_AD();

  testWiresOnByOne();

  //esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED) {
      // Woke from deep sleep
      returnFromDeepSleep();
      bReturnFromSleep = true;
      WiFi.mode(WIFI_OFF);
      btStop();
    
      Serial.println("returning from sleep");
      
  } else {
      // Power-on reset or other reset
      SetupNetworkStuff();
  }
  
    testWiresOnByOne();
    // Check for calibration
    PreferencesWrapper testerpreferences;
    testerpreferences.begin("Settings", false);
    IdleTimeToSleep = testerpreferences.getInt("TimeToSleep",90000);  // go to sleep after 90 seconds if no wires are plugged in
    TimeToDeepSleep = millis()+IdleTimeToSleep;
    testerpreferences.end();
    xTaskCreatePinnedToCore(
            TesterHandler,        /* Task function. */
            "TesterHandler",      /* String with name of task. */
            16384,                            /* Stack size in words. */
            NULL,                            /* Parameter passed as input of the task */
            0,                                /* Priority of the task. */
            &TesterTask,           /* Task handle. */
            1);
    esp_task_wdt_add(TesterTask);
    
    
}

void loop() {
  vTaskDelay(10 / portTICK_PERIOD_MS);
  if(!bReturnFromSleep){
    ElegantOTA.loop();
    esp_task_wdt_reset();
    terminal.loop();
  }
  esp_task_wdt_reset();
}


extern "C" void app_main() {
    // Call Arduino setup and loop
    initArduino(); // Initialize Arduino if needed
    setup(); 
    //printTasks(); // Print tasks during setup      // Call the Arduino setup function
    while (true) {
        loop();     // Call the Arduino loop function
    }
}