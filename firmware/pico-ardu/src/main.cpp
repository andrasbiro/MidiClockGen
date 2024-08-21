#include <Arduino.h>
#include "MIDI.h"
#include "RPi_Pico_TimerInterrupt.h"
#include "board.h"
#include "U8g2lib.h"
#include <Wire.h>
#include "RotaryEncoder.h"
#include <OneButton.h>
#include "LittleFS.h"
#include "VFS.h"
#include <hardware/gpio.h>


#define EEPROM_MAGIC 0xAB

struct EepromSettings {
  uint16_t bpm;
  uint8_t midiChannel;
  uint8_t magic;
};

RPI_PICO_Timer ITimer(0);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2,  MIDI);
U8G2_SSD1306_128X32_UNIVISION_F_2ND_HW_I2C u8g2(U8G2_R0);
RotaryEncoder encoder(PIN_ROT_A, PIN_ROT_B, RotaryEncoder::LatchMode::TWO03);
OneButton btn = OneButton(
  PIN_BTN,  // Input pin for the button
  true,        // Button is active LOW
  true         // Enable internal pull-up resistor
);


static long knobPosition = 0;
static long lastPosition = 0;


typedef enum {
  UI_MAIN,
  UI_SETCH,
} ui_state_t;


static uint16_t bpm = 100;
static uint16_t beat;
static int8_t listenChannel = 16;
static volatile bool bpmChanged = false;
static bool running = false;
static ui_state_t ui_state = UI_MAIN;

void printScreen(uint8_t drawColor = 1);


//clock and timing stuff
static inline float bpm2midihz(uint16_t bpm){
  return (float)24*bpm/60;
}

bool TimerCallback(struct repeating_timer *t){
  (void)t;
  MIDI.sendClock();
  beat++;
  if ( beat % 24 == 0 ){
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    beat==0;
  }
  return true;
}

void startClock(uint16_t bpm){
  if ( !running ){
    beat = 0;
    ITimer.attachInterrupt(bpm2midihz(bpm), TimerCallback);
    running = true;
    printScreen();
  }
}

void stopClock(){
  running = false;
  ITimer.stopTimer();
  printScreen();
  digitalWrite(PIN_LED, 0);
}

void bpmChange(){
  if ( running )
    ITimer.setFrequency(bpm2midihz(bpm), TimerCallback);
}

//display, ui
void printScreen(uint8_t drawColor ){
  char bpmstr[5];
  snprintf(bpmstr, 5, "%4d", bpm);
  u8g2.clearBuffer();					
  u8g2.setDrawColor(drawColor);
  u8g2.setFont(u8g2_font_logisoso20_tr);
  u8g2.drawStr(64,32,bpmstr);
  if ( listenChannel == MIDI_CHANNEL_OFF ){
    snprintf(bpmstr, 5, " Off");
  } else if ( listenChannel == MIDI_CHANNEL_OMNI ){
    snprintf(bpmstr, 5, " All");
  } else {
    snprintf(bpmstr, 5, "Ch%02d", listenChannel);
  }
  if ( ui_state == UI_MAIN ){
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(40,10,bpmstr);
    if ( running ){
      u8g2.drawDisc(16, 16, 8);
    }
  } else if ( ui_state == UI_SETCH ){
    u8g2.drawStr(5,32,bpmstr);
  }
  u8g2.sendBuffer();
}

void longPress(){
  if ( !running ){
    if ( ui_state == UI_MAIN){
      ui_state = UI_SETCH;
      printScreen();
    } else if ( ui_state == UI_SETCH ){
      EepromSettings settings;
      settings.bpm = bpm;
      settings.midiChannel = listenChannel;
      settings.magic = EEPROM_MAGIC;
      FILE *fp = fopen("/settings.cfg", "wb");
      if ( fp ){
        fwrite(&settings, sizeof(EepromSettings), 1, fp);
        fclose(fp);
      }
      ui_state = UI_MAIN;
      printScreen();
      MIDI.setInputChannel(listenChannel);
    }
  }
}

void shortPress(){
  if ( ui_state == UI_MAIN ){
    if ( !running ){
      startClock(bpm);
    } else {
      stopClock();
    }
  } else {
    //on the settings, short and longpress behaves the same
    longPress();
  }
}

void encodertick(){
  encoder.tick();
}

//midi read stuff
void ControlChangeCallback(midi::Channel channel, byte control, byte value){
  switch(control){
    case 10:
      //TODO fractional bpm?
      break;
    case 11:
      if ( value > 63 )
        bpm++;
      else
        bpm--;
      bpmChanged = true;
      break;
    case 12:
      if ( value > 63 )
        bpm+=10;
      else
        bpm-=10;
      bpmChanged = true;
      break;
    case 13:
      bpm = value*100;
      break;
    case 14:
      bpm += value;
      bpmChanged = true;
      break;
    case 15:
      if ( value > 63 ){
        startClock(bpm);
      } else {
        stopClock();
      }
      break;
  }
  if ( bpmChanged ){
    bpmChange();
  }
}




void setup() {
  pinMode(PIN_LED, OUTPUT);
  Serial.begin();


  Wire1.setSCL(PIN_SCL);
  Wire1.setSDA(PIN_SDA);
  u8g2.begin();
  encoder.setPosition(knobPosition);
  btn.attachClick(shortPress);
  btn.attachLongPressStop(longPress);
  LittleFS.begin();
  VFS.root(LittleFS);
  EepromSettings settings;
  if ( LittleFS.exists("/settings.cfg") ){
    settings.magic = 0;
    FILE *fp = fopen("/settings.cfg", "rb");
    if ( fp ){
      fread(&settings, sizeof(EepromSettings), 1, fp);
      fclose(fp);
      if ( settings.magic == EEPROM_MAGIC ){
        bpm = settings.bpm;
        listenChannel = settings.midiChannel;
      }
    }
  }
  if ( settings.magic != EEPROM_MAGIC ) {
    settings.magic = EEPROM_MAGIC;
    FILE *fp = fopen("/settings.cfg", "wb");
    if ( fp ){
      settings.bpm = bpm;
      settings.midiChannel = listenChannel;
      fwrite(&settings, sizeof(settings), 1, fp);
      fclose(fp);
    }
  }

  attachInterrupt(PIN_ROT_A, encodertick, CHANGE);
  attachInterrupt(PIN_ROT_B, encodertick, CHANGE);

  gpio_set_drive_strength(PIN_MIDI_TX, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_slew_rate(PIN_MIDI_TX, GPIO_SLEW_RATE_FAST);
  Serial2.setRX(PIN_MIDI_RX);
  Serial2.setTX(PIN_MIDI_TX);
  MIDI.begin(listenChannel);
  MIDI.setHandleControlChange(ControlChangeCallback);
  
  printScreen();
}

void loop1(){
  MIDI.read();

  btn.tick();
  knobPosition = encoder.getPosition();
  if ( knobPosition != lastPosition ){
    int16_t move = lastPosition - knobPosition;
    if ( ui_state == UI_MAIN ){
      bpm = bpm + move;
      bpmChanged = true;
      bpmChange();
    } else if ( ui_state == UI_SETCH ){
      listenChannel = listenChannel + move;
      if ( listenChannel < MIDI_CHANNEL_OMNI || listenChannel > 250 )
        listenChannel = MIDI_CHANNEL_OMNI;
      if ( listenChannel > MIDI_CHANNEL_OFF )
        listenChannel = MIDI_CHANNEL_OFF;
      printScreen();
    }
    lastPosition = knobPosition;
  }

  if (bpmChanged ){
    printScreen();
    bpmChanged = false;
  }
}


void loop() {
  //we don't do anything on this core, to make sure we have enough time for sending the clk
}
