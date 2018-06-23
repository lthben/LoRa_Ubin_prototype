/**
* Author: Benjamin Low (Lthben@gmail.com)
* @date 8 June 2018
* Description: Modified code of LoRa E32-TTL-100 Transceiver Interface
* by @author Bob Chen (bob-0505@gotmail.com)
* @date 1 November 2017
* https://github.com/Bob0505/E32-TTL-100
*
* USER NEEDS TO SPECIFY WHICH DEVICE THIS CODE IS FOR in E32-TTL-100.h
* Device_A: RECEIVER / simulating medical station with 'respond-to-distress' button
* Device_B: SENDER / simulating user station with 'call-for-help' button
*/
#include <SoftwareSerial.h>
#include <E32-TTL-100.h>
//OLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Button.h>

/*
need series a 4.7k Ohm resistor between .
UNO/NANO(5V mode)                E32-TTL-100
*--------*                      *------*
| D7     | <------------------> | M0   |
| D8     | <------------------> | M1   |
| A0     | <------------------> | AUX  |
| D10(Rx)| <---> 4.7k Ohm <---> | Tx   |
| D11(Tx)| <---> 4.7k Ohm <---> | Rx   |
*--------*                      *------*
*/

//E32-TTL-100 pins
#define M0_PIN	D7
#define M1_PIN	D8
#define AUX_PIN	A0
#define SOFT_RX	D6
#define SOFT_TX D5

SoftwareSerial softSerial(SOFT_RX, SOFT_TX);  // RX, TX

//button - D3
Button button = Button(D3, PULLUP);
String displayString = "";
bool hasActivatedSignal, isHelpComing;//User station
bool hasRcvdSignal, hasResponded; //Medical station
long sendTime; //User station. Send every sendInterval if no reply
const long SENDINTERVAL = 10000;
  uint8_t SOSmsg = 1; //0 - 255. User station number

//alert light - D4
#define ALERT_PIN D4

//OLED - D0, D1, D2
#define OLED_RESET D0
Adafruit_SSD1306 display(OLED_RESET);

#define NUMFLAKES 10
#define XPOS 0
#define YPOS 1
#define DELTAY 2

#if (SSD1306_LCDHEIGHT != 48) //64x48 pixels
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif


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

//for the oled screen
void displayText(long duration, bool hasDelay=true) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(displayString);
  display.display();
  if (hasDelay)
  {
    delay(duration);
    display.clearDisplay();
    display.display();
  }
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

  //OLED
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  // init done

  display.clearDisplay(); //prevent adafruit splash screen
  display.display();
  #ifdef Device_A
  displayString = "MEDICAL\n STATION";
  digitalWrite(ALERT_PIN, HIGH);
  #else
  displayString = "USER\n STATION";
  #endif
  displayText(2000);
}

// The loop function is called in an endless loop
void loop()
{
  uint8_t data_buf[100], data_len;

  /*************************************************
  * MEDICAL STATION
  *************************************************/
  #ifdef Device_A //RECEIVER "Medical station"
  if (ReceiveMsg(data_buf, &data_len)==RET_SUCCESS)
  {
    hasRcvdSignal = true;

    // blinkLED();
    digitalWrite(ALERT_PIN, HIGH);

    displayString = "";
    for (int i=0; i<sizeof(data_len); i++)
    {
      displayString += data_buf[i];
    }
    String prependStr = "Help\nrequested\nat station\n     #";
    displayString = prependStr + displayString;

    displayText(0,false);
    // Serial.print("Message received: ");
    // Serial.println(displayString);
  }
  else if (hasRcvdSignal==true && hasResponded==false)
  {
    if (button.uniquePress()) //comment out for automated response
    {
      digitalWrite(ALERT_PIN, LOW);

      hasResponded = true;

      SwitchMode(MODE_0_NORMAL);

      uint8_t ack = 255;
      SendMsg(ack);

      SwitchMode(MODE_2_POWER_SAVING);

      displayString = "\nAcknowledgement\n\nsent!";
      displayText(1000);
    }
  }
  else if (hasRcvdSignal==true and hasResponded==true)
  {
    hasRcvdSignal = false;
    hasResponded = false;
  }
  else
  {
    displayString = "on standby";
    displayText(0, false);
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
    displayString = "Help\nrequested.\nWaiting\nfor\nresponse";
    displayText(0,false);

    if (ReceiveMsg(data_buf, &data_len)==RET_SUCCESS)
    {
      if (*data_buf == 255)
      {
        displayString = "Message\nacknowledged.\nHelp is on\nthe way.";
        Serial.println("Message acknowledged");
        isHelpComing = true;
        displayText(4000);
      }
    }
    else if (millis() - sendTime > SENDINTERVAL) //send again if not received response
    {
      SendMsg(SOSmsg);
      sendTime = millis();
      displayString = "Sending\nagain ...";
      Serial.print("Message sent: ");
      Serial.println(SOSmsg);
      displayText(1000);
    }
  }
  else if (hasActivatedSignal == true && isHelpComing == true)
  {
    isHelpComing = false; //reset
    hasActivatedSignal = false;
    Serial.println("resetting");
    SwitchMode(MODE_2_POWER_SAVING);
  }
  else
  {
    displayString = "on standby";
    displayText(0, false);
  }
  #endif

  /*
  uint8_t data_buf[100], data_len;

  #ifdef Device_A
  if(ReceiveMsg(data_buf, &data_len)==RET_SUCCESS)
  {
    blinkLED();
    // Serial.println("Message received");
  }
  #else
  if(SendMsg()==RET_SUCCESS)
  {
    blinkLED();
    // Serial.print("Message sent");
  }
  #endif

  delay(random(400, 600));
  */
}
