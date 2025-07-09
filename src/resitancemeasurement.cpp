#include <Arduino.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_task_wdt.h"

// If you have different pins, change below defines

// Below table uses AD channels and not pin numbers
#define cl_analog ADC1_CHANNEL_0
#define bl_analog ADC1_CHANNEL_3
#define piste_analog ADC1_CHANNEL_6
#define cr_analog ADC1_CHANNEL_7
#define br_analog ADC1_CHANNEL_4
#define ar_analog ADC1_CHANNEL_5


#define al_driver 33
#define bl_driver 21
#define cl_driver 23
#define ar_driver 25
#define br_driver 05
#define cr_driver 18
#define piste_driver 19


// below defines are generated with the excel tool

#define IODirection_ar_br 231
#define IODirection_ar_cr 215
#define IODirection_ar_piste 183
#define IODirection_ar_bl 245
#define IODirection_ar_cl 243
#define IODirection_al_br 238
#define IODirection_al_cr 222
#define IODirection_al_piste 190
#define IODirection_al_bl 252
#define IODirection_al_cl 250
#define IODirection_br_cr 207
#define IODirection_br_bl 237
#define IODirection_br_cl 235
#define IODirection_br_piste 175
#define IODirection_bl_cl 249
#define IODirection_bl_piste 189
#define IODirection_bl_cr 221
#define IODirection_cr_piste 159
#define IODirection_cr_cl 219
#define IODirection_cl_piste 187
#define IODirection_cr_bl 221


#define IOValues_ar_br 8
#define IOValues_ar_cr 8
#define IOValues_ar_piste 8
#define IOValues_ar_bl 8
#define IOValues_ar_cl 8
#define IOValues_al_br 1
#define IOValues_al_cr 1
#define IOValues_al_piste 1
#define IOValues_al_bl 1
#define IOValues_al_cl 1
#define IOValues_br_cr 16
#define IOValues_br_bl 16
#define IOValues_br_cl 16 
#define IOValues_br_piste 16
#define IOValues_bl_cl 2
#define IOValues_bl_piste 2
#define IOValues_bl_cr 2
#define IOValues_cr_piste 32
#define IOValues_cr_cl 32
#define IOValues_cl_piste 4

#define IOValues_cr_bl 32

const uint8_t driverpins[] = {al_driver, bl_driver, cl_driver, ar_driver, br_driver, cr_driver, piste_driver};
int measurements[3][3];


void Set_IODirectionAndValue(uint8_t setting, uint8_t values)
{
  uint8_t mask = 1;
  for (int i = 0; i < 7; i++)
  {
    if (setting & mask)
    {
      pinMode(driverpins[i], INPUT);
    }
    else
    {
      pinMode(driverpins[i], OUTPUT);
      if (values & mask)
      {
        digitalWrite(driverpins[i], HIGH);
      }
      else
      {
        digitalWrite(driverpins[i], LOW);
      }

    }
    mask <<= 1;
  }
}

esp_adc_cal_characteristics_t adc_chars;
/*
int getSample(adc1_channel_t pin){
  int max = 0;
  int sample;
  for(int i = 0; i< 7; i++){
      //sample = analogReadMilliVolts(pin);
      sample = adc1_get_raw(pin);
      if(sample > max)
        max =sample;
  }
  return esp_adc_cal_raw_to_voltage(max, &adc_chars);
}
*/

int getDifferentialSample(adc1_channel_t pin1, adc1_channel_t pin2){
  //int min = 99999;
  int sample1 = 0;
  int sample2 =0;
  int delta;
  for(int i = 0; i< 8; i++){
    esp_task_wdt_reset();
      sample1 += adc1_get_raw(pin1);
    esp_task_wdt_reset();
      sample2 += adc1_get_raw(pin2);
  }
  delta = (esp_adc_cal_raw_to_voltage(sample1>>3, &adc_chars) - esp_adc_cal_raw_to_voltage(sample2>>3, &adc_chars));
  return delta;
}


extern const int Reference_3_Ohm[] = {3*16, 3*16, 3*16};
extern const int Reference_5_Ohm[] = {5*16, 5*16, 5*16};
extern const int Reference_10_Ohm[] = {10*16, 10*16, 10*16};



uint8_t testsettings[][3][2]={
  {{IODirection_cr_cl,IOValues_cr_cl},{IODirection_cr_piste,IOValues_cr_piste},{IODirection_cr_bl,IOValues_cr_bl}},
  {{IODirection_ar_cl,IOValues_ar_cl},{IODirection_ar_piste,IOValues_ar_piste},{IODirection_ar_bl,IOValues_ar_bl}},
  {{IODirection_br_cl,IOValues_br_cl},{IODirection_br_piste,IOValues_br_piste},{IODirection_br_bl,IOValues_br_bl}},
  
};
adc1_channel_t analogtestsettings[3]={cl_analog,piste_analog,bl_analog};
adc1_channel_t analogtestsettings_right[3]={cr_analog,ar_analog,br_analog};

void testWiresOnByOne()
{
  for(int Nr=0; Nr<3;Nr++)
  {
    for(int j=0;j<3;j++){
      Set_IODirectionAndValue(testsettings[Nr][j][0],testsettings[Nr][j][1]);
      measurements[Nr][j] = getDifferentialSample(analogtestsettings_right[Nr],analogtestsettings[j]);
    }
  }
  return;
}

bool WirePluggedIn(int threashold){ 
for(int Nr=0; Nr<3;Nr++)
  {
    for(int j=0;j<3;j++){
      
      if(measurements[Nr][j] < threashold){
        return true;}
    }
  }
  return false;
}

// Checks straigth connections only. Fills in measurement[i][i]
// Returns true if all connections have a resistance lower than 10 Ohm
bool testStraightOnly(int threashold) {
bool bOK = true;
for(int Nr=0; Nr<3;Nr++)
  {
    {
        Set_IODirectionAndValue(testsettings[Nr][Nr][0],testsettings[Nr][Nr][1]);
        
        //measurements[Nr][Nr] = getSample(analogtestsettings[Nr]);
        measurements[Nr][Nr] = getDifferentialSample(analogtestsettings_right[Nr],analogtestsettings[Nr]);
        if(measurements[Nr][Nr] > threashold)
          bOK = false;
    }
  }
  
  return bOK;

}

int testArBr() {

  Set_IODirectionAndValue(IODirection_ar_br,IOValues_ar_br);
  return(getDifferentialSample(ar_analog,br_analog));

}
int testArCr() {

  Set_IODirectionAndValue(IODirection_ar_cr,IOValues_ar_cr);
  return(getDifferentialSample(ar_analog,cr_analog));
}
int testArCl() {

  Set_IODirectionAndValue(IODirection_ar_cl,IOValues_ar_cl);
  return(getDifferentialSample(ar_analog,cl_analog));
}

int testBrCr() {

  Set_IODirectionAndValue(IODirection_br_cr,IOValues_br_cr);
  return(getDifferentialSample(br_analog,cr_analog));

}


// Simply broken: no contact between i-i' and no contact with other wires
bool IsBroken(int Nr, int threashold)
{
  if((measurements[Nr][Nr]<threashold))
    return false;
  for(int i= 0; i< 3; i++){
    if(i != Nr){
      if(measurements[Nr][i]< threashold)
        return false;
    }
  }
  return true;
}

bool IsSwappedWith(int i, int j, int threashold){
  if((measurements[i][j]<threashold) && (measurements[j][i]<threashold))
    return true;
  return false;
}



void init_AD(){
  gpio_set_drive_capability(GPIO_NUM_33, GPIO_DRIVE_CAP_3); // 40 mA
  gpio_set_drive_capability(GPIO_NUM_21, GPIO_DRIVE_CAP_3); // 40 mA
  gpio_set_drive_capability(GPIO_NUM_23, GPIO_DRIVE_CAP_3); // 40 mA
  gpio_set_drive_capability(GPIO_NUM_25, GPIO_DRIVE_CAP_3); // 40 mA
  gpio_set_drive_capability(GPIO_NUM_5, GPIO_DRIVE_CAP_3); // 40 mA
  gpio_set_drive_capability(GPIO_NUM_18, GPIO_DRIVE_CAP_3); // 40 mA
  gpio_set_drive_capability(GPIO_NUM_19, GPIO_DRIVE_CAP_3); // 40 mA


  Set_IODirectionAndValue(IODirection_ar_bl,IOValues_ar_bl);
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_3,ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_4,ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_5,ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_6,ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_DB_11);
  
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  int test = adc1_get_raw(ADC1_CHANNEL_3);
  test = adc1_get_raw(ADC1_CHANNEL_4);
  test = adc1_get_raw(ADC1_CHANNEL_5);
  test = adc1_get_raw(ADC1_CHANNEL_6);
  test = adc1_get_raw(ADC1_CHANNEL_7);
  test = adc1_get_raw(ADC1_CHANNEL_0);

}