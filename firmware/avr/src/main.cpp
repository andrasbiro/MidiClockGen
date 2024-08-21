#include <Arduino.h>

#ifdef ARDUINO_AVR_PROMICRO16
#define USB
#define LED_BUILTIN LED_BUILTIN_RX
#endif

#include <EEPROM.h>
#define EEPROM_ADDRESS 0
#define EEPROM_MAGIC 0xAB

struct EepromSettings {
  uint16_t bpm;
  uint8_t midiChannel;
  uint8_t magic;
};

#ifdef USB
#include <USB-MIDI.h>
#else
#include <MIDI.h>
#endif

#define USE_TIMER_1     true
#include <TimerInterrupt.h>
#include <ISR_Timer.h> 

#include "U8g2lib.h"
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include <Encoder.h>
#include <OneButton.h>

#ifdef USB 
USBMIDI_CREATE_DEFAULT_INSTANCE();
#else
MIDI_CREATE_DEFAULT_INSTANCE();
#endif

U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE); 

Encoder knob(3, 2);
static long knobPosition = 0;
static long lastPosition = 0;
OneButton btn = OneButton(
  A3,  // Input pin for the button
  true,        // Button is active LOW
  true         // Enable internal pull-up resistor
);

typedef enum {
  UI_MAIN,
  UI_SETCH,
} ui_state_t;

static uint16_t bpm = 100;
static uint16_t beat;
static int8_t listenChannel = 16;
static volatile bool bpmChanged = false;
static volatile bool toggleDisc = false;
static bool running = false;
static ui_state_t ui_state = UI_MAIN;

//clock and timing stuff
static inline float bpm2midihz(uint16_t bpm){
  return (float)24*bpm/60;
}


void TimerCallback(){
  MIDI.sendClock();
  beat++;
  if ( beat % 24 == 0 ){
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    toggleDisc = true;
    beat=0;
  }
}

void startClock(uint16_t bpm){
  if ( !running ){
    bpm = 0;
    ITimer1.attachInterrupt(bpm2midihz(bpm), TimerCallback);
    running = true;
  }
}

void stopClock(){
  running = false;
  ITimer1.stopTimer();
}



void printScreen(uint8_t drawColor = 1){
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
  } else if ( ui_state == UI_SETCH ){
    u8g2.drawStr(5,32,bpmstr);
  }
  u8g2.sendBuffer();
}

void bpmChange(){
  if ( running )
    ITimer1.setFrequency(bpm2midihz(bpm), TimerCallback);
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

void shortPress(){
  if ( ui_state == UI_MAIN ){
    if ( !running ){
      startClock(bpm);
    } else {
      stopClock();
    }
  } else {
    //TODO
  }
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
      EEPROM.put(EEPROM_ADDRESS, settings);
      ui_state = UI_MAIN;
      printScreen();
      MIDI.setInputChannel(listenChannel);
    }
  }
}


void setup() {
  EepromSettings settings;
  EEPROM.get(EEPROM_ADDRESS, settings);
  if ( settings.magic == EEPROM_MAGIC ){
    bpm = settings.bpm;
    listenChannel = settings.midiChannel;
  } else {
    settings.magic = EEPROM_MAGIC;
    settings.bpm = bpm;
    settings.midiChannel = listenChannel;
    EEPROM.put(EEPROM_ADDRESS, settings);
  }

  u8g2.begin();
  MIDI.begin(listenChannel);
  ITimer1.init();
  pinMode(LED_BUILTIN, OUTPUT);
  printScreen();

  knob.write(knobPosition);
  btn.attachClick(shortPress);
  btn.attachLongPressStop(longPress);

  MIDI.setHandleControlChange(ControlChangeCallback);
}


void loop() {
  MIDI.read();
  btn.tick();
  knobPosition = knob.read();
  if ( knobPosition != lastPosition ){
    int16_t move = lastPosition - knobPosition;
    if ( move >= 4 || move <= -4 ){
      move /= 4;
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
  }
  if ( bpmChanged ){
    printScreen();
    bpmChanged = false;
  }
  if ( toggleDisc ){
    toggleDisc = false;
    u8g2.setDrawColor(u8g2.getDrawColor()?0:1);
    u8g2.drawDisc(16, 16, 8);
    u8g2.sendBuffer();
  }
}
