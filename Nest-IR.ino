//
// Nest Heat Pump IR Control
//
// Monitors the HVAC signals from an attached Nest Thermostat,
// and sends the appropriate IR commands to the heatpump.
//
// Note: You can configure the Nest in two modes:
// - Conventional Heat/Cool
// - Heat Pump (OB)
// The */OB and Y1 signals behave differently in these two setups.
//
// https://github.com/cyborg5/IRLib2
// https://github.com/cyborg5/IRLib2/blob/master/IRLib2/manuals/IRLibReference.pdf
// https://learn.adafruit.com/using-an-infrared-library/hardware-needed
//
// https://github.com/ToniA/arduino-heatpumpir
//   NOTE: You may have to delete IRSenderPWM.cpp in order to compile
//

//#include <IRLibAll.h>
#include <MitsubishiHeatpumpIR.h>


////////// USER PREFERENCES /////////////////////////////////////////

// You can edit the following to suit your tastes...

// Temperature:     18..31 Deg C
// Operating Modes: MODE_AUTO, MODE_HEAT, MODE_COOL, MODE_DRY, MODE_FAN, MODE_MAINT
// Fan Speeds:      FAN_AUTO, FAN_1, FAN_2, FAN_3, FAN_4
// Vertical Swing:  VDIR_AUTO, VDIR_MANUAL, VDIR_SWING, VDIR_UP, VDIR_MUP, VDIR_MIDDLE, VDIR_MDOWN, VDIR_DOWN

// Heating, Stage 1
#define R_HEAT1_TEMPERATURE (22)
#define R_HEAT1_FANSPEED    (FAN_AUTO)
#define R_HEAT1_VANEDIR     (VDIR_AUTO)

// Heating, Stage 2
#define R_HEAT2_TEMPERATURE (31)
#define R_HEAT2_FANSPEED    (FAN_4)
#define R_HEAT2_VANEDIR     (VDIR_MIDDLE)

// Cooling, Stage 1
#define R_COOL1_TEMPERATURE (16)
#define R_COOL1_FANSPEED    (FAN_AUTO)
#define R_COOL1_VANEDIR     (VDIR_AUTO)

// Fan Mode
#define R_FANMODE_FANSPEED  (FAN_4)
#define R_FANMODE_VANEDIR   (VDIR_SWING)

// Dry Mode
#define R_DRYMODE_FANSPEED  (FAN_AUTO)
#define R_DRYMODE_VANEDIR   (VDIR_AUTO)

////////// HARDWARE CONFIGURATION ///////////////////////////////////

// Uncomment to enable IR receiver + debug output on serial terminal.
//#define ENABLE_IR_DIAGNOSTICS

#define BTN_1 (14) // Send current state via IR
#define BTN_2 (16) // Power off

#define NEST_W1 (5)  // Heat
#define NEST_Y1 (6)  // Cool
#define NEST_G  (7)  // Fan
#define NEST_OB (8)  // Heat2, Cool2, or Heatpump Power (O/B)

#define IR_RX_PIN (2) // IR RX (Supports 0,1,2,3,7)
#define IR_TX_PIN (9) // IR TX

////////// DEFINES //////////////////////////////////////////////////

#define IR_REPEAT_CODE 0xFFFFFFFF

// Supported states
typedef enum {
	HP_STATE_OFF,
	HP_STATE_HEAT1,
	HP_STATE_HEAT2,
	HP_STATE_COOL,
	HP_STATE_FAN,
	HP_STATE_DRY
} heatpump_states_t;

// Possible input states
typedef enum {
	sOFF=0,	// Logic low (0V)
	sON=1,  // Logic high (24V)
	sANY	// Any / Don't care
} signal_state_t;

// When Nest is configured for heat pump mode, 
// the OB signal switches between heat & cool. 
// You should configure the nest for 'O' mode (not 'B' mode), which maps to the below:
#define sHEAT (sOFF)
#define SCOOL (sON)

// Input HVAC state
typedef union {
	struct {
		signal_state_t w1: 2;
		signal_state_t y1: 2;
		signal_state_t g: 2;
		signal_state_t ob: 2;
	} __attribute__ ((packed));
	uint8_t raw;
} hvac_state_t;

typedef struct {
	hvac_state_t      hvac_state;
	heatpump_states_t dest_state;
} hvac_state_map_t;


////////// STATE TRANSITION MAP /////////////////////////////////////

// Map the HVAC (Nest) state to Heat Pump state (Conventional HVAC Config)
// The star wire is used to signal Stage 2 Heating or Cooling.
// The Nest will use Heat2 if the target temperature is much higher than the current
// (eg. if temperature difference is only 1-2 deg C, only Heat1 will be used)
hvac_state_map_t hvac_state_map[] = {
	//  W1       Y1        G      *|OB  (Nest Wiring)
	//  Heat1    Cool      Fan    Heat2
	{ { sON,     sOFF,     sANY,  sOFF }, HP_STATE_HEAT1 },
	{ { sON,     sOFF,     sANY,  sON  }, HP_STATE_HEAT2 },
	{ { sOFF,    sON,      sANY,  sANY }, HP_STATE_COOL },
	{ { sOFF,    sOFF,     sON,   sANY }, HP_STATE_FAN }, // You can swap this with HP_STATE_DRY if you want
	{ { sOFF,    sOFF,     sOFF,  sOFF }, HP_STATE_OFF }
};

/*
// Map the HVAC (Nest) state to Heat Pump state (Heat Pump Config)
// The OB/Y1 signals are used to indicate the heat pump state.
// The Nest behaves slightly differently in this config, and may delay up to 2 minutes before
// switching between heat/cool, though the heatpump already has protections for this.
// It's really intended for homes with dumber heat pump HVAC systems.
hvac_state_map_t hvac_state_map[] = {
	//  W1       Y1        G      *|OB  (Nest Wiring)
	//  AuxHeat  HPump     Fan    Heat/Cool
	{ { sOFF,    sON,      sANY,  sHEAT }, HP_STATE_HEAT1 },
	{ { sON,     sON,      sANY,  sHEAT }, HP_STATE_HEAT2 },
	//{ { sON,     sOFF,     sANY,  sANY  }, HP_STATE_HEAT2 }, // "Emergency Heat"
	{ { sANY,    sON,      sANY,  sCOOL }, HP_STATE_COOL },
	{ { sANY,    sOFF,     sON,   sANY  }, HP_STATE_FAN },
	{ { sOFF,    sOFF,     sOFF,  sANY  }, HP_STATE_OFF }
	
};*/
/*
Cool:
HVAC State: (W:0 Y:0 G:1 OB:1) [OB:0] -> [OB:1]
HVAC State: (W:0 Y:1 G:1 OB:1) [Y:0] -> [Y:1]

Cool, Fan only:
HVAC State: (W:0 Y:0 G:1 OB:1) [Y:1] -> [Y:0]

Heat, counting down:
HVAC State: (W:1 Y:0 G:1 OB:1) [W:0] -> [W:1]

Heating:
HVAC State: (W:0 Y:0 G:1 OB:0) [OB:1] -> [OB:0]
HVAC State: (W:0 Y:1 G:1 OB:0) [Y:0] -> [Y:1]

Heating, full:
HVAC State: (W:1 Y:1 G:1 OB:0) [W:0] -> [W:1]
*/


////////// GLOBALS //////////////////////////////////////////////////

// State variables
hvac_state_t      currHvacState     = {0};
heatpump_states_t currHeatPumpState = HP_STATE_OFF;
bool              btn1State         = LOW;
bool              btn2State         = LOW;

// IR RX (NOTE: Doesn't work with our heatpump remote)
#ifdef ENABLE_IR_DIAGNOSTICS
IRrecvPCI irrx(IR_RX_PIN);	// RX on pin 2 (Supports 0,1,2,3,7)
IRdecode  irdecode;
#endif

// IR TX
//IRsend    irtx; 			// TX on pin 9

// Heat Pump IR 
IRSenderBitBang irSender(IR_TX_PIN);
HeatpumpIR *heatpumpIR;


////////// MAIN CODE ////////////////////////////////////////////////

//
// Print current HVAC state
// eg. "W:1 Y:0 G:1 OB:0"
//
void printHvacState(hvac_state_t hvacState, uint8_t hvacMask = 0xFF) {
	hvac_state_t hvacMaskState;
	hvacMaskState.raw = hvacMask;
	bool x = false;
	
	if (hvacMaskState.w1) {
		Serial.print("W:"); Serial.print(hvacState.w1); 
		x = true;
	}
	if (hvacMaskState.y1) {
		if (x) { Serial.print(" "); }
		Serial.print("Y:"); Serial.print(hvacState.y1);
		x = true;
	}
	if (hvacMaskState.g) {
		if (x) { Serial.print(" "); }
		Serial.print("G:"); Serial.print(hvacState.g);
		x = true;
	}
	if (hvacMaskState.ob) {
		if (x) { Serial.print(" "); }
		Serial.print("OB:"); Serial.print(hvacState.ob);
	}
}

//
// Print current Heat Pump state
//
void printHeatPumpState(heatpump_states_t state) {
	switch (state) {
		case HP_STATE_OFF:
			Serial.print("Off"); break;
		case HP_STATE_HEAT1:
			Serial.print("Heat1"); break;
		case HP_STATE_HEAT2:
			Serial.print("Heat2"); break;
		case HP_STATE_COOL:
			Serial.print("Cool"); break;
		case HP_STATE_FAN:
			Serial.print("Fan"); break;
		case HP_STATE_DRY:
			Serial.print("Dry"); break;
		default:
			Serial.print("UNKNOWN"); break;
	}
}

//
// Send command to heat pump.
// NOTE: Heat pump may not register two back-to-back commands.
//
void sendHeatpumpCmd(
	uint8_t powerModeCmd, uint8_t operatingModeCmd, uint8_t fanSpeedCmd, uint8_t temperatureCmd, uint8_t swingVCmd, uint8_t swingHCmd)
{
	// Print, for debug purposes
	Serial.print("IR Cmd: {");
	if (powerModeCmd == POWER_OFF) {
		Serial.print("Power Off");
	}
	else {
		// Print mode
		Serial.print("Mode:");
		switch (operatingModeCmd) {
			case MODE_AUTO:  Serial.print("AUTO"); break;
			case MODE_COOL:  Serial.print("COOL"); break;
			case MODE_HEAT:  Serial.print("HEAT"); break;
			case MODE_DRY:   Serial.print("DRY"); break;
			case MODE_FAN:   Serial.print("FAN"); break;
			case MODE_MAINT: Serial.print("MAINTENANCE"); break;
		}
		
		// Print temperature
		if (operatingModeCmd != MODE_FAN && operatingModeCmd != MODE_DRY) {
			Serial.print(" Temp:"); Serial.print(temperatureCmd); Serial.print("C");
		}
		
		// Print fan speed
		Serial.print(" FanSpeed:"); 
		if (fanSpeedCmd == FAN_AUTO) {
			Serial.print("AUTO");
		} else {
			Serial.print(fanSpeedCmd);
		}
		
		// Print vertical vane dir
		Serial.print(" Vane:");
		switch (swingVCmd) {
			case VDIR_AUTO:   Serial.print("AUTO"); break;
			case VDIR_SWING:  Serial.print("SWING"); break;
			case VDIR_UP:     Serial.print("UP"); break;
			case VDIR_MUP:    Serial.print("M-UP"); break;
			case VDIR_MIDDLE: Serial.print("MIDDLE"); break;
			case VDIR_MDOWN:  Serial.print("M-DOWN"); break;
			case VDIR_DOWN:   Serial.print("DOWN"); break;
		}
	}
	Serial.println("}");
	
	// Send IR command packet
	heatpumpIR->send(irSender, powerModeCmd, operatingModeCmd, fanSpeedCmd, temperatureCmd, swingVCmd, swingHCmd);
	
	// The heatpump may not register two back-to-back commands.
	// As a workaround, add a delay to ensure the heat pump has time to
	// register the above command before sending another.
	delay(1000);
}


//
// Called on transition to a Heat Pump state.
// Sends the appropriate IR command to the Heat Pump.
//
void transitionTo(heatpump_states_t newState) {
	Serial.print("Transition: ");
	printHeatPumpState(currHeatPumpState);
	Serial.print(" -> ");
	printHeatPumpState(newState);
	Serial.println();
	
	// send(irSender, powerMode, operatingMode, fanSpeed, temperature, verticalSwing, horizontalSwing)
	
	// MODIFY TO SUIT YOUR TASTES:
	switch (newState) {
		//                             Power     Mode       Fan     Temp.  Angle
		case HP_STATE_HEAT1:
			sendHeatpumpCmd( 
				POWER_ON,
				MODE_HEAT,
				R_HEAT1_FANSPEED,
				R_HEAT1_TEMPERATURE,
				R_HEAT1_VANEDIR,
				HDIR_AUTO);
			break;
		case HP_STATE_HEAT2:
			sendHeatpumpCmd( 
				POWER_ON, 
				MODE_HEAT, 
				R_HEAT2_FANSPEED, 
				R_HEAT2_TEMPERATURE, 
				R_HEAT2_VANEDIR, 
				HDIR_AUTO);
			break;
		case HP_STATE_COOL:
			sendHeatpumpCmd( 
				POWER_ON, 
				MODE_COOL,
				R_COOL1_FANSPEED,
				R_COOL1_TEMPERATURE, 
				R_COOL1_VANEDIR, 
				HDIR_AUTO);
			break;
		case HP_STATE_FAN:
			sendHeatpumpCmd( 
				POWER_ON,				
				MODE_FAN,
				R_FANMODE_FANSPEED, 
				21, // N/A
				R_FANMODE_VANEDIR, 
				HDIR_AUTO);
			break;
		case HP_STATE_DRY:
			sendHeatpumpCmd( 
				POWER_ON,
				MODE_DRY,
				R_DRYMODE_FANSPEED, 
				21, // N/A
				R_DRYMODE_VANEDIR, 
				HDIR_AUTO);
			break;
		case HP_STATE_OFF:
			sendHeatpumpCmd(POWER_OFF, MODE_COOL, FAN_AUTO, 21, VDIR_UP, HDIR_AUTO);
			break;
	}
	
	currHeatPumpState = newState;
}

//
// Button 1 pressed
//
void btn1Pressed() {
	Serial.println("Button1 - Re-transmit State");
	printHeatPumpState(currHeatPumpState); Serial.println();
	transitionTo(currHeatPumpState);
}

//
// Button 2 pressed
//
void btn2Pressed() {
	Serial.println("Button2 - Turn Off");
	printHeatPumpState(currHeatPumpState); Serial.println();
	transitionTo(HP_STATE_OFF);
}

//
// Process the Heat Pump state map to determine if 
// we need to transition to another state
//
void processHvacStateMap(hvac_state_t newHvacState) {	
	for (auto& entry: hvac_state_map) {
		//Serial.print(entry.w1); Serial.print(entry.y1); Serial.print(entry.g); Serial.print(entry.ob); 
		//Serial.print(":");
		//Serial.print(w1); Serial.print(y1); Serial.print(g); Serial.print(ob); 
		//Serial.println();
		
		// Check if we know how to transition from this state
		if (((entry.hvac_state.w1 == sANY) || (entry.hvac_state.w1 == newHvacState.w1)) &&
		    ((entry.hvac_state.y1 == sANY) || (entry.hvac_state.y1 == newHvacState.y1)) &&
			((entry.hvac_state.g  == sANY) || (entry.hvac_state.g  == newHvacState.g)) &&
			((entry.hvac_state.ob == sANY) || (entry.hvac_state.ob == newHvacState.ob)))
		{
			heatpump_states_t newHeatPumpState = entry.dest_state;
			
			// Ignore transition if it ends up in the same state
			if (newHeatPumpState != currHeatPumpState) {
				transitionTo(newHeatPumpState);
				return;
			}
		}
	}
}


//
// Detect any changes to the HVAC signals
//
void processHvac(bool changed = false) {
	hvac_state_t newHvacState = {0};
	
	newHvacState.w1 = digitalRead(NEST_W1);
	newHvacState.y1 = digitalRead(NEST_Y1);
	newHvacState.g  = digitalRead(NEST_G);
	newHvacState.ob = digitalRead(NEST_OB);
	
	// Input state has changed
	if (newHvacState.raw != currHvacState.raw) {
		uint8_t hvacStateChange = newHvacState.raw ^ currHvacState.raw;
		
		Serial.print("HVAC State: (");
		printHvacState(newHvacState);
		Serial.print(") [");
		
		printHvacState(currHvacState, hvacStateChange);
		Serial.print("] -> [");
		printHvacState(newHvacState, hvacStateChange);
		Serial.println("]");
		
		//processStateTable(newHvacState);
		processHvacStateMap(newHvacState);
		currHvacState = newHvacState;
	}
}

void processButtons() {
	bool btn1 = digitalRead(BTN_1);
	bool btn2 = digitalRead(BTN_2);
	
	// TODO: De-bouncing would probably be a good idea
	
	if (btn1 != btn1State) {
		btn1State = btn1;
		if (btn1 == HIGH) {
			btn1Pressed();
		}
	}
	if (btn2 != btn2State) {
		btn2State = btn2;
		if (btn2 == HIGH) {
			btn2Pressed();
		}
	}
	
}

//
// Application Init
//
void setup() {
	// Configure HVAC signals
	pinMode(NEST_W1, INPUT);
	pinMode(NEST_Y1, INPUT);
	pinMode(NEST_G, INPUT);
	pinMode(NEST_OB, INPUT);
	
	pinMode(BTN_1, INPUT);
	pinMode(BTN_2, INPUT);
	
	pinMode(2, INPUT); // IR RX
	
	// Our heatpump is actually an MSZ-GE, not an MSZ-FE. But a google image search seems to suggest they're more or less the same thing, and this seems to work fine.
	heatpumpIR = new MitsubishiFEHeatpumpIR();
	
	Serial.begin(115200);
	
#ifdef ENABLE_IR_DIAGNOSTICS
	// Enable IR receiver
	irrx.enableIRIn();
#endif

	// Transition to the initial state
	transitionTo(HP_STATE_OFF);
}

//
// Main Loop
//
void loop() {
	
	// Process NEST HVAC Signals
	processHvac();

	// Process Buttons
	processButtons();
	
#ifdef ENABLE_IR_DIAGNOSTICS
	// Process IR RX (For debugging)
	if (irrx.getResults()) {
		//decode_results results;
		//irrx.decode(&results);
		irdecode.decode();
		
		// Ignore NEC repeat codes
		//if ((irdecode.value != IR_REPEAT_CODE) && (irdecode.protocolNum != 0)) {
			Serial.print("IR RX:");
			Serial.print(irdecode.protocolNum);
			Serial.print(":");
			Serial.print(irdecode.value, 16);
			Serial.print(":");
			Serial.print(irdecode.bits);
			Serial.println();
			irdecode.dumpResults(false);
			
			irtx.send(irdecode.protocolNum, irdecode.value, irdecode.bits);
			//irrx.enableIRIn();
		//}
		
		irrx.enableIRIn();
	}
#endif
}
