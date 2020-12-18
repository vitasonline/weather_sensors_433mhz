#include "esphome.h"
// China SL-TX583 WWW.HH.COM 433Mhz weather sensor decoder.
// __           ___       ___    ___
//   |         |  |      |  |   |  |
//   |_________|  |______|  |___|  |
//
//   |  Sync      |    1    |  0   |
//   |  9000us    | 4250us  | 1750us
// Defines
const int data433Pin = 4;
#define DataBits0 4                                       // Number of data0 bits to expect
#define DataBits1 32                                      // Number of data1 bits to expect
#define allDataBits 36                                    // Number of data sum 0+1 bits to expect
// isrFlags bit numbers
#define F_HAVE_DATA 1                                     // 0=Nothing in read buffer, 1=Data in read buffer
#define F_GOOD_DATA 2                                     // 0=Unverified data, 1=Verified (2 consecutive matching reads)
#define F_CARRY_BIT 3                                     // Bit used to carry over bit shift from one long to the other
#define F_STATE 7                                         // 0=Sync mode, 1=Data mode
// Constants
const unsigned long sync_MIN = 8000;                      // Minimum Sync time in micro seconds
const unsigned long sync_MAX = 10000;
const unsigned long bit1_MIN = 2500;
const unsigned long bit1_MAX = 6000;
const unsigned long bit0_MIN = 1000;
const unsigned long bit0_MAX = 2500;
const unsigned long glitch_Length = 700;                  // Anything below this value is a glitch and will be ignored.
// Interrupt variables
unsigned long fall_Time = 0;                              // Placeholder for microsecond time when last falling edge occured.
unsigned long rise_Time = 0;                              // Placeholder for microsecond time when last rising edge occured.
byte bit_Count = 0;                                       // Bit counter for received bits.
unsigned long build_Buffer[] = {0,0};                     // Placeholder last data packet being received.
volatile unsigned long read_Buffer[] = {0,0};             // Placeholder last full data packet read.
volatile byte isrFlags = 0;   


class WeatherSensors : public PollingComponent, public Sensor {
 public:

  WeatherSensors() : PollingComponent(5000) { }

  void setup() override {
  pinMode(data433Pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(data433Pin),PinChangeISR0,CHANGE);
  }

  void update() override {
    
    unsigned long myData0 = 0;
    unsigned long myData1 = 0;
    unsigned long T;
    if (bitRead(isrFlags,F_GOOD_DATA) == 1)
    {
      // We have at least 2 consecutive matching reads
      myData0 = read_Buffer[0]; // Read the data spread over 2x 32 variables
      myData1 = read_Buffer[1];
      bitClear(isrFlags,F_HAVE_DATA); // Flag we have read the data
      dec2binLong(myData0,DataBits0);
      dec2binLong(myData1,DataBits1);
      Serial.print(" - ID=");
      byte ID = (myData1 >> 24) & 0xFF;   // Get ID
      Serial.print(ID);
      Serial.print(" - Battery=");
      byte B = (myData1 >> 23) & 0x1;   // Get Battery
      Serial.print(B);
      Serial.print(" - TX=");
      byte TX = (myData1 >> 22) & 0x1;   // Get TX
      Serial.print(TX);
      Serial.print(" Channel=");
      byte CH = ((myData1 >> 20) & 0x3) + 1;     // Get Channel
      Serial.print(CH);
      Serial.print(" Temperature=");
      unsigned long T = (myData1 >> 8) & 0xFFF; // Get Temperature
      Serial.print(T/10.0,1);
      Serial.print("C Humidity=");
      byte H = (myData1 >> 0) & 0xFF;       // Get LLLL
      Serial.print(H);
      Serial.println("%");
    }

    publish_state(T); 
  }

private:

  // Various flag bits
  void dec2binLong(unsigned long myNum, byte NumberOfBits) {
    if (NumberOfBits <= 32){
      myNum = myNum << (32 - NumberOfBits);
      for (int i=0; i<NumberOfBits; i++) {
        if (bitRead(myNum,31) == 1)
        Serial.print("1");
        else
        Serial.print("0");
        myNum = myNum << 1;
      }
    }
  }
  void ICACHE_RAM_ATTR PinChangeISR0();

  void PinChangeISR0(){                                     // Pin 2 (Interrupt 0) service routine
    unsigned long Time = micros();     // Get current time
    if (digitalRead(data433Pin) == HIGH) {                             // Set 'HIGH' some receivers
  // Falling edge
      if (Time > (rise_Time + glitch_Length)) {
  // Not a glitch
        Time = micros() - fall_Time;     
        // Subtract last falling edge to get pulse time.
        if (bitRead(build_Buffer[1],31) == 1)
          bitSet(isrFlags, F_CARRY_BIT);
        else
          bitClear(isrFlags, F_CARRY_BIT);
          //Serial.println(isrFlags,BIN);
        if (bitRead(isrFlags, F_STATE) == 1) {
  // Looking for Data
  //Serial.println(Time);
          if ((Time > bit0_MIN) && (Time < bit0_MAX)) {
  // 0 bit
            build_Buffer[1] = build_Buffer[1] << 1;
            build_Buffer[0] = build_Buffer[0] << 1;
            if (bitRead(isrFlags,F_CARRY_BIT) == 1)
              bitSet(build_Buffer[0],0);
            bit_Count++;
          }
          else if ((Time > bit1_MIN) && (Time < bit1_MAX)) {
  // 1 bit
            build_Buffer[1] = build_Buffer[1] << 1;
            bitSet(build_Buffer[1],0);
            build_Buffer[0] = build_Buffer[0] << 1;
            if (bitRead(isrFlags,F_CARRY_BIT) == 1)
              bitSet(build_Buffer[0],0);
            bit_Count++;
          }
          else {
  // Not a 0 or 1 bit so restart data build and check if it's a sync?
            bit_Count = 0;
            build_Buffer[0] = 0;
            build_Buffer[1] = 0;
            bitClear(isrFlags, F_GOOD_DATA);                // Signal data reads dont' match
            bitClear(isrFlags, F_STATE);                    // Set looking for Sync mode
            if ((Time > sync_MIN) && (Time < sync_MAX)) {
              // Sync length okay
              bitSet(isrFlags, F_STATE);                    // Set data mode
            }
          } //Serial.println(bit_Count);
          if (bit_Count >= allDataBits) {
  // All bits arrived
            bitClear(isrFlags, F_GOOD_DATA);                // Assume data reads don't match
            if (build_Buffer[0] == read_Buffer[0]) {
              if (build_Buffer[1] == read_Buffer[1])
                bitSet(isrFlags, F_GOOD_DATA);              // Set data reads match
            }
            read_Buffer[0] = build_Buffer[0];
            read_Buffer[1] = build_Buffer[1];
            bitSet(isrFlags, F_HAVE_DATA);                  // Set data available
            bitClear(isrFlags, F_STATE);                    // Set looking for Sync mode
            build_Buffer[0] = 0;
            build_Buffer[1] = 0;
            bit_Count = 0;
          }
        }
        else {
  // Looking for sync
          if ((Time > sync_MIN) && (Time < sync_MAX)) {
  // Sync length okay
            build_Buffer[0] = 0;
            build_Buffer[1] = 0;
            bit_Count = 0;
            bitSet(isrFlags, F_STATE);                      // Set data mode
          }
        }
        fall_Time = micros();                               // Store fall time
      }
    }
    else {
  // Rising edge
      if (Time > (fall_Time + glitch_Length)) {
        // Not a glitch
        rise_Time = Time;                                   // Store rise time
      }
    }
  }

};

