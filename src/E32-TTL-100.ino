/**
* Author: Benjamin Low (Lthben@gmail.com)
* @date 14 August 2018
* Description: prototype of a single channel one-to-one LoRa channel
* for emergency communications
*
* LoRa E32-TTL-100 Transceiver Interface
* by @author Bob Chen (bob-0505@gotmail.com)
* @date 1 November 2017
* https://github.com/Bob0505/E32-TTL-100
*
* need series a 4.7k Ohm resistor between .
* UNO/NANO(5V mode)                E32-TTL-100
* *--------*                      *------*
* | 28     | <------------------> | M0   |
* | 26     | <------------------> | M1   |
* | A0     | <------------------> | AUX  |
* | 10 (Rx)| <---> 4.7k Ohm <---> | Tx   |
* | 11 (Tx)| <---> 4.7k Ohm <---> | Rx   |
* *--------*                      *------*
*
* LCD library:
*       https://github.com/olikraus/u8glib
*
* LCD product page:
*       http://www.continental.sg/lcd/products/lcd-128x64
*
* 128x64 LCD to Arduino Mega connections:-
*       VSS - GND
*       VDD - 5V
*       RS/CS - 8 (any pin)
*       R/W/MOSI - 51
*       E/SCK - 52
*       PSB - GND
*       RST - 9 (any pin)
*       BLA - 5V
*       BLK - GND
*/

//USER NEEDS TO SPECIFY WHICH DEVICE THIS CODE IS FOR BY COMMENTING OUT ONE BELOW
#define Device_A // receiver (medical station with 'respond-to-distress' button)
// #define Device_B // sender (user station with 'call-for-help' button)

#include <SoftwareSerial.h>
#include <E32-TTL-100.h>
//OLED
#include <SPI.h>
#include <Wire.h>
#include <Button.h>
#include <U8glib.h>

//LCD screen
U8GLIB_ST7920_128X64_1X u8g(52, 51, 8, 9);    // SPI Com: SCK = en = 18, MOSI = rw = 16, CS = di = 17
unsigned long lastShownTime; //blinking effect
unsigned long emergencyButtonPressTime;
unsigned long acknowledgeButtonPressTime;
bool isShow;
int screenModeNum; //which screen text to display

//E32-TTL-100 pins
#define M0_PIN	28
#define M1_PIN	26
#define AUX_PIN	A0
#define SOFT_RX	10
#define SOFT_TX 11

SoftwareSerial softSerial(SOFT_RX, SOFT_TX);  // RX, TX

//button - 3
Button button = Button(3, PULLUP);
bool hasActivatedSignal, isHelpComing;//User station
bool hasRcvdSignal, hasResponded; //Medical station
unsigned long sendTime; //User station. Send every sendInterval if no reply
const long SENDINTERVAL = 10000;
uint8_t SOSmsg = 1; //0 - 255. User station number

//alert light - 4
#define ALERT_PIN 4

//=== Sleep mode cmd ================================-

RET_STATUS SettingModule(struct CFGstruct *pCFG)
{
  RET_STATUS STATUS = RET_SUCCESS;

  #ifdef Device_A
  pCFG->ADDH = DEVICE_A_ADDR_H;
  pCFG->ADDL = DEVICE_A_ADDR_L;
  #else
  pCFG->ADDH = DEVICE_B_ADDR_H;
  pCFG->ADDL = DEVICE_B_ADDR_L;
  #endif

  //USER-DEFINED
  pCFG->OPTION_bits.trsm_mode =TRSM_FP_MODE;
  pCFG->OPTION_bits.tsmt_pwr = TSMT_PWR_20DB;
  pCFG->OPTION_bits.wakeup_time = WAKE_UP_TIME_1000;
  pCFG->OPTION_bits.drive_mode = PP_DRIVE_MODE;
  pCFG->SPED_bits.air_bps = AIR_BPS_300; //decrease air rate to increase comms range

  STATUS = SleepModeCmd(W_CFG_PWR_DWN_SAVE, (void* )pCFG);

  SleepModeCmd(W_RESET_MODULE, NULL);

  STATUS = SleepModeCmd(R_CFG, (void* )pCFG);

  return STATUS;
}

RET_STATUS ReceiveMsg(uint8_t *pdatabuf, uint8_t *data_len)
{
  RET_STATUS STATUS = RET_SUCCESS;
  uint8_t idx;

  *data_len = softSerial.available();

  if (*data_len > 0)
  {
    Serial.print("ReceiveMsg: ");  Serial.print(*data_len);  Serial.println(" bytes.");

    for(idx=0;idx<*data_len;idx++)
    *(pdatabuf+idx) = softSerial.read();

    for(idx=0;idx<*data_len;idx++)
    {
      Serial.print(" 0x");
      Serial.print(0xFF & *(pdatabuf+idx), HEX);    // print as an ASCII-encoded hexadecimal
    } Serial.println("");
  }
  else
  {
    STATUS = RET_NOT_IMPLEMENT;
    // Serial.println("line 353: RET_NOT_IMPLEMENT");
  }

  return STATUS;
}

RET_STATUS SendMsg(uint8_t pdatabuf)
{
  RET_STATUS STATUS = RET_SUCCESS;

  if(ReadAUX()!=HIGH)
  {
    return RET_NOT_IMPLEMENT;
  }
  delay(10);
  if(ReadAUX()!=HIGH)
  {
    return RET_NOT_IMPLEMENT;
  }

  //USER-DEFINED
  //TRSM_FP_MODE
  //Send format : ADDH ADDL CHAN DATA_0 DATA_1 DATA_2 ...
  // uint8_t randomByte = random(0x00,0xFF);

  #ifdef Device_A
  uint8_t SendBuf[4] = { DEVICE_B_ADDR_H, DEVICE_B_ADDR_L, 0x17, pdatabuf };	//for A
  #else
  uint8_t SendBuf[4] = { DEVICE_A_ADDR_H, DEVICE_A_ADDR_L, 0x17, pdatabuf };	//for B
  #endif
  softSerial.write(SendBuf, 4);

  return STATUS;
}

//for the LED_BUILTIN
void blinkLED()
{
  static bool LedStatus = LOW;

  digitalWrite(LED_BUILTIN, LedStatus);
  LedStatus = !LedStatus;
}

/********************************************************************
*
*   MAIN PROGRAMME
*
********************************************************************/

//The setup function is called once at startup of the sketch
void setup()
{
  RET_STATUS STATUS = RET_SUCCESS;
  struct CFGstruct CFG;
  struct MVerstruct MVer;

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  // pinMode(LED_BUILTIN, OUTPUT);

  softSerial.begin(9600);
  Serial.begin(9600);

  #ifdef Device_A
  Serial.println("[10-A] ");
  pinMode(ALERT_PIN, OUTPUT);
  #else
  Serial.println("[10-B] ");
  #endif

  STATUS = SleepModeCmd(R_CFG, (void* )&CFG);
  STATUS = SettingModule(&CFG);
  STATUS = SleepModeCmd(R_MODULE_VERSION, (void* )&MVer);

  SwitchMode(MODE_2_POWER_SAVING);

  //self-check initialization.
  WaitAUX_H();
  delay(10);

  if(STATUS == RET_SUCCESS)
  Serial.println("Setup init OK!!");

  #ifdef Device_A
  digitalWrite(ALERT_PIN, LOW);
  screenModeNum = 6; //default screen for Device A (medical station): on standby
  #else
  screenModeNum = 0; //default screen for Device B (user station): instructions to press button
  #endif
}

void u8g_prepare(void) {
  u8g.setFont(u8g_font_6x10);
  u8g.setFontRefHeightExtendedText();
  u8g.setDefaultForegroundColor();
  u8g.setFontPosTop();
}

void draw() {
  u8g_prepare();
  switch(screenModeNum) {

    case(0): //user station default: instructions on pressing emergency button
    u8g.drawStr(14, 8, "EMERGENCY BUTTON");
    if (isShow) {
      u8g.setScale2x2();
      u8g.drawStr(4, 12, "PRESS FOR");
      u8g.drawStr(20, 20, "HELP");
      u8g.undoScale();
    }
    break;

    case(1): //user station: after pressing emergency button
    u8g.drawStr(24, 20, "Help requested");
    u8g.drawStr(4, 32, "Waiting for response");
    if (isShow) {
      u8g.drawStr(52, 44, "...");
    }
    break;

    case(2): //user station: message acknowledged
    u8g.drawStr(4, 20, "Message acknowledged");
    u8g.drawStr(12, 32, "Help is on the way");
    break;

    case(3): //user station: sending again
    u8g.drawStr(24, 24, "Sending again");
    break;

    case(4): //medical station: instructions to acknowledge
    u8g.drawStr(8, 2, "Help requested at");
    u8g.drawStr(24, 12, "station #XX");
    if (isShow) {
      u8g.setScale2x2();
      u8g.drawStr(8, 12, "PRESS TO");
      u8g.drawStr(0, 20, "ACKNOWLEDGE");
      u8g.undoScale();
    }
    break;

    case(5): //medical station: Acknowledgement sent
    u8g.drawStr(20, 18, "Acknowledgement");
    u8g.drawStr(48, 30, "sent");
    break;

    case(6): //medical station default: on standby
    u8g.drawStr(32, 24, "On standby");
    if (isShow) {
      u8g.drawStr(94, 24, "...");
    }
    break;
  }
}

void loop()
{
  uint8_t data_buf[100], data_len;

  // picture loop
  u8g.firstPage();
  do {
    draw();
  } while ( u8g.nextPage() );

  // for blinking effect
  if (millis() - lastShownTime > 1000)
  {
    isShow = !isShow;
    lastShownTime = millis();
  }
  /*************************************************
  * MEDICAL STATION
  *************************************************/
  #ifdef Device_A //RECEIVER "Medical station"
  if (ReceiveMsg(data_buf, &data_len)==RET_SUCCESS)
  {
    hasRcvdSignal = true;

    // blinkLED();
    digitalWrite(ALERT_PIN, HIGH);

    screenModeNum = 4;

    Serial.println("Message received.");
  }
  else if (hasRcvdSignal==true && hasResponded==false)
  {
    if (button.uniquePress()) //comment out for automated response
    {
      digitalWrite(ALERT_PIN, LOW);

      hasResponded = true;

      SwitchMode(MODE_1_WAKE_UP);

      uint8_t ack = 255;
      if (SendMsg(ack) == RET_SUCCESS) {

        screenModeNum = 5;
        acknowledgeButtonPressTime = millis();

        Serial.println("Acknowledgement sent!");
        SwitchMode(MODE_2_POWER_SAVING);
      }
      else
      {
          Serial.println("Failed to send!");
      }
    }
  }
  else if (hasRcvdSignal==true and hasResponded==true)
  {
    hasRcvdSignal = false;
    hasResponded = false;
  }
  else
  {
    if (millis() - acknowledgeButtonPressTime > 4000)
    {
      screenModeNum = 6;
    }
    digitalWrite(ALERT_PIN, LOW);
    // Serial.println("on standby");
  }

  /************************************
  * USER STATION
  ************************************/
  #else //Device_B SENDER
  if (button.uniquePress() && hasActivatedSignal == false && isHelpComing == false)
  {
    hasActivatedSignal = true;
    emergencyButtonPressTime = millis();

    SwitchMode(MODE_1_WAKE_UP);

    if (SendMsg(SOSmsg)==RET_SUCCESS)
    {
      Serial.print("Message sent: ");
      Serial.println(SOSmsg);
      sendTime = millis();
    }
  }
  else if (hasActivatedSignal == true && isHelpComing == false)
  {
    if (millis() - sendTime < 2000 && millis() - emergencyButtonPressTime > 2000) {
      screenModeNum = 3; //notify sent again
    } else {
      screenModeNum = 1;
    }

    if (ReceiveMsg(data_buf, &data_len)==RET_SUCCESS) //acknowledge by medical station
    {
      if (*data_buf == 255)
      {
        screenModeNum = 2;
        acknowledgeButtonPressTime = millis();
        Serial.println("Message acknowledged");
        isHelpComing = true;
      }
    }
    else if (millis() - sendTime > SENDINTERVAL) //send again if not received response
    {
      SendMsg(SOSmsg);
      sendTime = millis();
      Serial.print("Message sent: ");
      Serial.println(SOSmsg);
    }
  }
  else if (hasActivatedSignal == true && isHelpComing == true)
  {
    if (millis() - acknowledgeButtonPressTime > 4000) //time to show help coming message
    {
      screenModeNum = 0; //back to default
      isHelpComing = false; //reset
      hasActivatedSignal = false;
      Serial.println("resetting");
      SwitchMode(MODE_2_POWER_SAVING);
    }
  }
  else
  {
    screenModeNum = 0;
  }
  #endif
}
