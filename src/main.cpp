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
#include "USBSerialTerminal.h"

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
int StoredIdleTimeToSleep = 90000;  // Default 90 seconds
int CalibrationDisplayChannel = 0;  // Default to channel 0
bool CalibrationAutoMode = false;  // Auto mode flag


AsyncWebServer server(80);
SettingsManager settings;
WebTerminal terminal(server);
USBSerialTerminal serialTerminal; // Add this global variable

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

// Forward declarations
void synchronizeThresholdValues();
void LoadSettings();

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
 
  if(delayAndTestWirePluggedIn(250))
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


ITerminal* currentTerminal = nullptr;

void handleCalibrateCommand(ITerminal* term, const std::vector<String>& args) {
    currentTerminal = term;  // Set global pointer
    if (args.empty()) {
        term->printf("Calibrate command requires 'on' or 'off' [channel]\n");
        term->printf("Usage: calibrate on [0-2]  or  calibrate off\n");
        term->printf("Default: auto mode (tracks lowest value channel)\n");
        return;
    }

    if (args[0] == "on") {
        DoCalibration = true;
        
        // Set which channel to display
        if (args.size() > 1) {
            if (args[1] == "auto") {
                CalibrationAutoMode = true;
                CalibrationDisplayChannel = 0;
                term->printf("Calibration started in AUTO mode (lowest value channel)\n");
            } else {
                int channel = args[1].toInt();
                if (channel >= 0 && channel <= 2) {
                    CalibrationAutoMode = false;
                    CalibrationDisplayChannel = channel;
                    term->printf("Calibration started for channel %d\n", CalibrationDisplayChannel);
                } else {
                    term->printf("Invalid channel %d. Using AUTO mode.\n", channel);
                    CalibrationAutoMode = true;
                    CalibrationDisplayChannel = 0;
                    term->printf("Calibration started in AUTO mode (lowest value channel)\n");
                }
            }
        } else {
            CalibrationAutoMode = true;
            CalibrationDisplayChannel = 0;
            term->printf("Calibration started in AUTO mode (lowest value channel)\n");
        }
        
    } else if (args[0] == "off") {
        DoCalibration = false;
        
    } else {
        term->printf("Unknown argument for Calibrate command\n");
    }
}

// Function to synchronize myRefs_Ohm with StoredRefs_ohm after settings changes
void synchronizeThresholdValues() {
  for(int i = 0; i < 11; i++){
    myRefs_Ohm[i] = StoredRefs_ohm[i];
  }
}

void AdjustThreasholdForRealV() {
  static bool bInitialAdjustmentDone = false;
  static long TimeToTest = 0;
  
  // Only perform initial adjustment once at startup
  if(bInitialAdjustmentDone) {
    return;
  }
  
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
    bInitialAdjustmentDone = true; // Mark as completed
  } else {
    // If conditions aren't met, try again in 1 second
    TimeToTest = millis() + 1000;
  }
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
        currentTerminal->printf("Duration = %d\n",duration);
        
        // Update min/max for all channels (needed for calculations)
        for(int i = 0; i<3;i++){
            if(min[i] > SortedSamples[i][0])
                min[i] = (SortedSamples[i])[0];
            
            if(max[i] < SortedSamples[i][NR_SAMPLES-1] )
                max[i] = SortedSamples[i][NR_SAMPLES-1] ;
        }
        
        // Auto mode: find channel with lowest median value
        if (CalibrationAutoMode) {
            int bestChannel = 0;
            long lowestMedian = SortedSamples[0][NR_SAMPLES/2];
            
            for (int ch = 1; ch < 3; ch++) {
                long median = SortedSamples[ch][NR_SAMPLES/2];
                if (median < lowestMedian) {
                    lowestMedian = median;
                    bestChannel = ch;
                }
            }
            
            // Update display channel if it changed
            if (bestChannel != CalibrationDisplayChannel) {
                CalibrationDisplayChannel = bestChannel;
                currentTerminal->printf("AUTO: Switched to channel %d (lowest median: %ld)\n", 
                               CalibrationDisplayChannel, lowestMedian);
            }
        }
        
        // Display results only for the selected channel
        int i = CalibrationDisplayChannel;
        
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
        
        // Show mode in the output
        String modeStr = CalibrationAutoMode ? " (AUTO)" : "";
        currentTerminal->printf("Channel %d%s:\n", i, modeStr.c_str());
        currentTerminal->printf("   Min = %d      Max = %d      P50 = %d      P90 = %d\n", 
                        SortedSamples[i][0], SortedSamples[i][NR_SAMPLES-1], 
                        SortedSamples[i][percentile_50_index], SortedSamples[i][percentile_90_index]);
        currentTerminal->printf("   Most common = %d (%dx)      Best triplet = %d (%dx) P%d\n", 
                    mostCommonValue, maxCount, bestTripletCenter, bestTripletSum, tripletPercentile);
        currentTerminal->printf("   **BEST ADC = %d**      Trimmed mean = %d\n", 
                    bestADCValue, trimmedMean);
    }
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
      // AdjustThreasholdForRealV(); // Removed from main loop - now only runs at startup
      if(WirePluggedIn()){
        State = WireTesting_1;
        NoWireTimeout = NO_WIRES_PLUGGED_IN_TIMEOUT;
        timetoswitch = WIRE_TEST_1_TIMEOUT;

      }
      esp_task_wdt_reset();
      if(TimeToDeepSleep < millis()){
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
void handleListCommand(ITerminal* term, const std::vector<String>& args) {
    term->printf("Available settings:\n");
    term->printf("===================\n");
    
    term->printf("Integer settings:\n");
    term->printf("  IdleTimeToSleep     : %d ms (Idle time before sleep)\n", StoredIdleTimeToSleep);
    term->printf("  R0                  : %d ohm (Total resistance Ron + 2x47)\n", R0);
    term->printf("  Vmax                : %d mV (Maximum voltage)\n", Vmax);
    
    term->printf("\nBoolean settings:\n");
    term->printf("  bCalibrate          : %s (Perform Calibration?)\n", CalibrationEnabled ? "true" : "false");
    
    term->printf("\nString settings:\n");
    term->printf("  name                : %s (Device Name)\n", deviceName.c_str());
    
    term->printf("\nArray settings:\n");
    term->printf("  myRefs_Ohm          : [");
    for(int i = 0; i < 11; i++) {
        term->printf("%d", StoredRefs_ohm[i]);
        if(i < 10) term->printf(", ");
    }
    term->printf("] (Threshold values 0-10 Ohm)\n");
    
    term->printf("\nNote: Use the web interface to modify these settings\n");
    term->printf("Current working values:\n");
    term->printf("  myRefs_Ohm (active) : [");
    for(int i = 0; i < 11; i++) {
        term->printf("%d", myRefs_Ohm[i]);
        if(i < 10) term->printf(", ");
    }
    term->printf("]\n");
    term->printf("  IdleTimeToSleep     : %ld ms (active)\n", IdleTimeToSleep);
}


void handleSetCommand(ITerminal* term, const std::vector<String>& args) {
    if (args.size() < 2) {
        term->printf("Usage: set <setting_name> <value>\n");
        term->printf("Available settings: IdleTimeToSleep, R0, Vmax, bCalibrate, name, myRefs_Ohm\n");
        term->printf("Example: set IdleTimeToSleep 120000\n");
        term->printf("Example: set name \"MyTester\"\n");
        term->printf("Example: set myRefs_Ohm 0,1,2,3,4,5,6,7,8,9,12\n");
        return;
    }

    String settingName = args[0];
    String value = args[1];
    
    term->printf("Setting '%s' to '%s'...\n", settingName.c_str(), value.c_str());
    
    // Integer settings
    if (settingName == "IdleTimeToSleep") {
        int newValue = value.toInt();
        if (newValue <= 0) {
            term->printf("Error: IdleTimeToSleep must be > 0\n");
            return;
        }
        StoredIdleTimeToSleep = newValue;
        IdleTimeToSleep = newValue;  // Update working variable too
        term->printf("✓ Set IdleTimeToSleep = %d ms\n", newValue);
        
    } else if (settingName == "R0") {
        int newValue = value.toInt();
        if (newValue <= 0) {
            term->printf("Error: R0 must be > 0\n");
            return;
        }
        R0 = newValue;
        term->printf("✓ Set R0 = %d ohm\n", newValue);
        
    } else if (settingName == "Vmax") {
        int newValue = value.toInt();
        if (newValue <= 0) {
            term->printf("Error: Vmax must be > 0\n");
            return;
        }
        Vmax = newValue;
        term->printf("✓ Set Vmax = %d mV\n", newValue);
        
    // Boolean settings
    } else if (settingName == "bCalibrate") {
        if (value == "true" || value == "1") {
            CalibrationEnabled = true;
            term->printf("✓ Set bCalibrate = true\n");
        } else if (value == "false" || value == "0") {
            CalibrationEnabled = false;
            term->printf("✓ Set bCalibrate = false\n");
        } else {
            term->printf("Error: bCalibrate must be 'true' or 'false'\n");
            return;
        }
        
    // String settings
    } else if (settingName == "name") {
        // Remove quotes if present
        if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.substring(1, value.length() - 1);
        }
        deviceName = value;
        term->printf("✓ Set name = \"%s\"\n", value.c_str());
        
    // Array settings
    } else if (settingName == "myRefs_Ohm") {
        // Parse comma-separated values
        int newRefs[11];
        int count = 0;
        int startPos = 0;
        
        for (int i = 0; i < 11; i++) {
            int commaPos = value.indexOf(',', startPos);
            String numberStr;
            
            if (commaPos == -1) {
                // Last number
                numberStr = value.substring(startPos);
            } else {
                numberStr = value.substring(startPos, commaPos);
                startPos = commaPos + 1;
            }
            
            newRefs[i] = numberStr.toInt();
            count++;
            
            if (commaPos == -1) break; // No more commas
        }
        
        if (count != 11) {
            term->printf("Error: myRefs_Ohm requires exactly 11 values\n");
            return;
        }
        
        // Validate values are in ascending order
        for (int i = 1; i < 11; i++) {
            if (newRefs[i] < newRefs[i-1]) {
                term->printf("Error: Values must be in ascending order\n");
                return;
            }
        }
        
        // Update both stored and working arrays
        for (int i = 0; i < 11; i++) {
            StoredRefs_ohm[i] = newRefs[i];
            myRefs_Ohm[i] = newRefs[i];
        }
        
        term->printf("✓ Set myRefs_Ohm = [");
        for (int i = 0; i < 11; i++) {
            term->printf("%d", newRefs[i]);
            if (i < 10) term->printf(",");
        }
        term->printf("]\n");
        
    } else {
        term->printf("Error: Unknown setting '%s'\n", settingName.c_str());
        term->printf("Available: IdleTimeToSleep, R0, Vmax, bCalibrate, name, myRefs_Ohm\n");
        return;
    }
    
    // Save to flash with feedback
    term->printf("Saving to flash...\n");
    settings.save();
    term->printf("✓ Settings successfully saved to flash!\n");
    term->printf("✓ Setting change complete.\n");
}




void LoadSettings() {
  // Register settings
  settings.addInt("IdleTimeToSleep", "Idle time before sleep (ms)", &StoredIdleTimeToSleep);
  settings.addBool("bCalibrate", "Perform Calibration?", &CalibrationEnabled);
  settings.addIntArray("myRefs_Ohm", "Threshold values from 0 - 10 Ohm", StoredRefs_ohm, 11);
  settings.addInt("R0", "R0 (total resistance (Ron + 2 x 47)", &R0);
  settings.addInt("Vmax", "Vmax in mV", &Vmax);
  settings.addString("name", "Device Name", &deviceName);
  settings.begin("Settings");        // for Preferences namespace
  settings.load();
  
  // Copy the loaded stored references to working references (AFTER settings.load())
  for(int i = 0; i< 11; i++){
      myRefs_Ohm[i]= StoredRefs_ohm[i];
  }
  // Copy the loaded idle time to working variable, but ensure it's not 0
  if(StoredIdleTimeToSleep > 0) {
    IdleTimeToSleep = StoredIdleTimeToSleep;
  } else {
    // If not set in NVS yet, use default and save it
    IdleTimeToSleep = 90000;  // 90 seconds default
    StoredIdleTimeToSleep = IdleTimeToSleep;
    settings.save();  // Save the default value to NVS
  }
}

void SetupNetworkStuff(){
  Serial.println("Not returning from sleep");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Tester", "01041967");
    ElegantOTA.begin(&server);    // Start ElegantOTA
    Serial.println(WiFi.softAPIP());
    terminal.begin();
  // Register command handlers
  terminal.registerCommand("echo", [](ITerminal* term, const std::vector<String>& args) {
        String response;
        for (auto& arg : args) {
            response += arg + " ";
        }
        term->send(response);
    });

    terminal.registerCommand("reboot", [](ITerminal* term, const std::vector<String>&) {
        term->send("Rebooting...");
        ESP.restart();
    });
    
    terminal.registerCommand("calibrate", handleCalibrateCommand);
    terminal.registerCommand("list", handleListCommand);
    terminal.registerCommand("set", handleSetCommand);

    terminal.registerCommand("help", [](ITerminal* term, const std::vector<String>&) {
        term->send("Available commands:");
        term->send("  echo <text>          - Echo back the text");
        term->send("  reboot               - Restart the device");
        term->send("  calibrate on [0-2]   - Start calibration (optional channel)");
        term->send("  calibrate on auto    - Start auto calibration (tracks lowest channel)");
        term->send("  calibrate off        - Stop calibration");
        term->send("  list                 - Show available settings");
        term->send("  set <name> <value>   - Change a setting");
        term->send("  help                 - Show this help message");
    });

    server.begin();
  terminal.printf("HTTP server started\n");
  
  // Load settings
  LoadSettings();
  
  settings.addWebEndpoints(server);  // to set up web routes
  
  // Register callback to synchronize threshold values when settings change via web
  settings.setPostSaveCallback(synchronizeThresholdValues);

}

void setupSerialTerminal() {
    serialTerminal.begin();
    
    // Register the same commands as WebTerminal
    serialTerminal.registerCommand("echo", [](ITerminal* term, const std::vector<String>& args) {
        String response;
        for (auto& arg : args) {
            response += arg + " ";
        }
        term->send(response + "\n");
    });

    serialTerminal.registerCommand("reboot", [](ITerminal* term, const std::vector<String>&) {
        term->send("Rebooting...\n");
        ESP.restart();
    });
    
    serialTerminal.registerCommand("calibrate", handleCalibrateCommand);
    serialTerminal.registerCommand("list", handleListCommand);
    serialTerminal.registerCommand("set", handleSetCommand);

    serialTerminal.registerCommand("help", [](ITerminal* term, const std::vector<String>&) {
        term->send("Available commands:\n");
        term->send("  echo <text>          - Echo back the text\n");
        term->send("  reboot               - Restart the device\n");
        term->send("  calibrate on [0-2]   - Start calibration (optional channel)\n");
        term->send("  calibrate on auto    - Start auto calibration (tracks lowest channel)\n");
        term->send("  calibrate off        - Stop calibration\n");
        term->send("  list                 - Show available settings\n");
        term->send("  set <name> <value>   - Change a setting\n");
        term->send("  help                 - Show this help message\n");
    });
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
  
  // Setup serial terminal first, before other initialization
  setupSerialTerminal();
  
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
      
      // Load settings when waking from sleep
      LoadSettings();
      
  } else {
      // Power-on reset or other reset
      SetupNetworkStuff();
  }
  
    testWiresOnByOne();
    // Check for calibration
    //PreferencesWrapper testerpreferences;
    //testerpreferences.begin("Settings", false);
    //IdleTimeToSleep = testerpreferences.getInt("TimeToSleep",90000);  // go to sleep after 90 seconds if no wires are plugged in
    TimeToDeepSleep = millis()+IdleTimeToSleep;
    //testerpreferences.end();
    
    // Perform initial threshold adjustment after ADC initialization and settings load
    AdjustThreasholdForRealV();
    
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
  
  // Always run serial terminal
  serialTerminal.loop();
  
  if(!bReturnFromSleep){
    ElegantOTA.loop();
    esp_task_wdt_reset();
    terminal.loop(); // WebTerminal only when not returning from sleep
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