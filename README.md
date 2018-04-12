# Nest-IR

An Arduino interface between Nest's HVAC signals and an IR-controlled heat pump.

This is designed specifically for a [Nest model E thermostat](https://nest.com/thermostats/nest-thermostat-e/overview/) & Mitsubishi MSZ-GE heat pump, 
but could be adapted for any IR-controlled heat pump via the excellent [Arduino-HeatpumpIR](https://github.com/ToniA/arduino-heatpumpir) library.
A little more work may be required if you want to make this work with the original Nest (which has 6 HVAC signals).

## How it works

The Arduino monitors the HVAC signals from the Nest, and detects what state the heat pump should be in.
When the Nest changes the HVAC state the Arduino emits an IR command to put the heat pump in the right mode.

Since the HVAC signals are pretty dumb, you have to decide what temperature/fanspeed/direction is suitable for each mode.
For my apartment I chose Heating Stage 1 to heat to 22C, and Heating Stage 2 to heat to 31C with full fan speed. The Nest will use Stage 2 
to rapidly bring the room up to temperature, then drop back to Stage 1 as it nears the target.

The biggest downside is that the IR remote that comes with the heatpump cannot know the current state of the heat pump, but since the Arduino 
only sends IR commands on HVAC transitions, you are free to override the heat pump state with the original remote.

There are two optional buttons:

- Button 1 - Re-send the current Nest state, in case you've overidden the state via the original IR remote.
- Button 2 - Turn the heat pump off, regardless of the Nest state.

## Required hardware:

- An Arduino of some sort. I'm using a Sparkfun Pro Mini.
- An IR emitter LED
- A 24V supply for the Nest. HVAC is usually AC, but DC works fine too.
- Four voltage divided inputs for the HVAC signals (for the Nest-E. Other Nests have 6 signals). 
  Since the Nest's outputs are 24V HVAC logic, you must ensure these fall within the 3.3V or 5.0V logic of the Arduino.

## A quick guide to HVAC signals:

All HVAC signals have 24V logic. (0V - inactive, 24V - active)

- W1 - Heating, Stage 1
- Y1 - Cooling, Stage 1
- G - Fan (If W1 or Y1 is active, G should also be active)
- C - Common / Ground (24V AC, or GND)
- R/Rh/Rc - Power supply (24V AC, or +24V DC)
- */OB - On the Nest-E, this can be used for either W2/Y2 (conventional configuration), or O/B (heat pump configuration).
  - W2 - Heating, Stage 2
  - Y2 - Cooling, Stage 2
  - O/B - Heat pump Heat/Cool mode. B is the inverse of O. In O/B mode, Y1 becomes the heatpump on/off control, and W1 becomes "emergency heat".
  
Important: The Nest is clever and uses the thickness of the wires that you insert to detect whether that wire is connected.
This means that stranded wire will not work (unless it's very thick stranded wire). I soldered my wires (UTP cable) to power diode leads, 
which are thick enough to be detected by the Nest.

**Code is provided as-is; you're on your own if you want to use it!**
