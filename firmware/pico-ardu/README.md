This project is working, although it is currently in the prototype stage. 

TODO:
- Get some "EEPROM" working. I don't want to use the arduino EEPROM
  implementation for the pico as it has no wear-levelling
- Clean up the codebase. It is currently a mess. The UI and midi stuff should be
  put in dedicated classes probably
- Make sure arduino MIDI is thread safe and/or make it thread safe. We're
  writing to midi out from both cores of the pico, which could cause issues

There is an alternate plan if I have the time: Implement the midi part of the
code at least in PIO. I think MIDI through can be implemented in PIO completely,
and we just need one additional FIFO to write real time midi messages in the
middle of the stream (or one more, if we want to generalize it for not just real
time midi). It might end up being on native pico-sdk, not arduino.