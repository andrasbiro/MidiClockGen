This project likely does not work. Originally it had a bug that it only sent out
every 24th midi clk it should have. Although that is fixed, I doubt the AVR is
fast enough for this: I could only get it running on a pico by dedicating one
core to the clk sending.

Developement now is on the pico based board, so this is abandoned.