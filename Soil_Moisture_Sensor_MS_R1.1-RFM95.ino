
/*  
 *  Soil Moisture Sensor
 *    4 channels
 *    Temperature + Humidity 
 *    1 Analog In
 *    1 Flow Counter In
 *    DS3232 RTC + Temperature
 *  
 *  Water from a pressure transducer, 0 to 5v
 *    Input fron sensor is scaled by .6577, 5.0v * .6577 = 3.288v
 *    (10.2k and a 19.6k resistor, flitered with a .1uf cap)
 *   
 *  Output: 0.5V – 4.5V linear voltage output. 0 psi outputs 0.5V, 50 psi outputs 2.5V, 100 psi outputs 4.5V 
 *    0   psi = 0.33v after scalling 5.0v to 3.3v
 *    50  psi = 1.65v
 *    100 psi = 2.97v
 *    3.3v/1023 = .003225806 volt per bit or 3.3V/4095 = .000805861 volts per bit
 *    
 *
 *    Max6816 is used as an option on Rev2a board to de-bounce the flow reed switch on the meter
 *    The switch must be stable for 40ms to get an output, this limits the max
 *    rate this device can support to less than 15Hz or so.
 *
 *
 * CHANGE LOG:
 *
 *  DATE         REV  DESCRIPTION
 *  -----------  ---  ----------------------------------------------------------
 *  22-Dec-2016 1.0a  TRL - first 
 *  27-Dec-2016 1.1a  TRL - Added DS3232 and Moisture Mux code
 *  
 *
 *  Notes:  1)  Tested with Arduino 1.6.13
 *          2)  Testing using RocketStream M0 with RFM95
 *          3)  Sensor 2 board, Rev2a 
 *          4)  MySensor 2.1b 4 Dec 2016
 *    
 *    
 *    MCP9800   base I2C address 0x40
 *    Si7021    base I2C address 0x48
 *    DS3231    base I2C address 0x68
 *    EUI64     base I2C address 0x50
 *    
 *    TODO:   RFM95 in sleep mode RFM_Write(0x01,0x00); 
 *            Flash in sleep mode
 *            Sleep Mode
 *            WDT
 *            Set correct alarms time and functions
 *            Send current time to board
 *    
 *    
 */
/* ************************************************************************************** */
// Define's for the board options
#define R2A                   // Rev 2a of the board

// Select the Temperature and/or Humidity sensor on the board
//#define Sensor_SI7021       // Using the Si7021 Temp and Humidity sensor
//#define Sensor_MCP9800        // Using the MCP9800 temp sensor
//#define WaterPressure       // if we have a water pressure sensor

//#define MyWDT               // Watch Dog Timer
#define MoistureSensor        // Moisture Sensor, 4 channels
#define MyDS3231               // DS3231 RTC

#define MY_SERIALDEVICE Serial  // this will override Serial port define in MyHwSAMD.h file

/* ************************************************************************************** */
#include <Arduino.h>
#include <Wire.h>

#if defined MyDS3231
    #include <DS3231.h>
    #include "LowPower.h"
#endif

#if defined Sensor_SI7021
    #include "i2c_SI7021.h"
    SI7021 si7021;
#endif

#if defined Sensor_MCP9800
    #include <MCP980X.h>      // http://github.com/JChristensen/MCP980X
    MCP980X MCP9800(0);
#endif

#if defined MyWDT
  #include <avr/wdt.h>      // for watch-dog timer support
#endif

#if defined MoistureSensor
  #include <math.h>                 // Conversion equation from resistance to %
#endif 

/* ************************************************************************************** */
// Most of these items below need to be prior to #include <MySensor.h> 

/*  Enable debug prints to serial monitor on port 0 */
#define MY_DEBUG            // used by MySensor
//#define MY_SPECIAL_DEBUG
//#define MY_DEBUG_VERBOSE_RFM95 
#define MY_DEBUG1           // used in this program, level 1 debug
//#define MY_DEBUG2           // used in this program, level 2 debug

#define SKETCHNAME      "Soil Moisture Sensor"
#define SKETCHVERSION   "1.1a"

/* ************************************************************************************** */
/* Enable and select radio type attached, coding rate and frequency
 *  
 *   Pre-defined MySensor radio config's
 *   
 * | CONFIG           | REG_1D | REG_1E | REG_26 | BW    | CR  | SF   | Comment
 * |------------------|--------|--------|--------|-------|-----|------|-----------------------------
 * | BW125CR45SF128   | 0x72   | 0x74   | 0x04   | 125   | 4/5 | 128  | Default, medium range SF7
 * | BW500CR45SF128   | 0x92   | 0x74   | 0x04   | 500   | 4/5 | 128  | Fast, short range     SF7
 * | BW31_25CR48SF512 | 0x48   | 0x94   | 0x04   | 31.25 | 4/8 | 512  | Slow, long range      SF9
 * | BW125CR48SF4096  | 0x78   | 0xC4   | 0x0C   | 125   | 4/8 | 4096 | Slow, long range      SF12
 */

#define MY_RADIO_RFM95
#define MY_RFM95_MODEM_CONFIGRUATION    RFM95_BW125CR45SF128
#define MY_RFM95_TX_POWER               13 // max is 23
//#define MY_RFM95_ATC_MODE_DISABLED
#define MY_RFM95_ATC_TARGET_RSSI        (-60)
#define MY_RFM95_FREQUENCY              (915.0f)
#define SendDelay                       0
#define AckFlag                         false

/* ************************************************************************************** */
// Select correct defaults for the processor and board we are using
#ifdef __SAMD21G18A__                 // useing an ARM M0 Processsor, Zero, Feather M0, RocketScream Mini Pro
//#ifdef defined(ARDUINO_ARCH_SAMD)
#warning Using an ARM M0 SAMD21G18A Processsor

//#define MY_RFM95_RST_PIN        RFM95_RST_PIN
#define MY_RFM95_IRQ_PIN          2               // IRQ
#define MY_RFM95_SPI_CS           5               // NSS
#define MY_DEFAULT_TX_LED_PIN     12              // Led's on the board
#define MY_DEFAULT_ERR_LED_PIN    10
#define MY_DEFAULT_RX_LED_PIN     11
#define OnBoardLed                13              // Cpu Led

#define MY_WITH_LEDS_BLINKING_INVERSE

#define OnBoardFlash                4             // Flash chip on processor

#define ID0                         0             // ID0 pin
#define ID1                         1
#define ID2                         6

#else
  #error ********* Processor not defined
#endif


/* ************************************************************************************** */

//#define MY_REPEATER_FEATURE

/* ************************************************************************************** */
// Set node defaults
#define NodeID_Base          20         // My Node ID base... this plus IDx bits
int myNodeID =                0;        // Set at run time from jumpers on PCB
#define MY_NODE_ID myNodeID             // Set at run time from jumpers

#define MY_PARENT_NODE_ID     0         // GW ID

#define CHILD_ID1             1         // Id of my Water sensor child
#define CHILD_ID2             2         // Id of my 2nd sensor child
#define CHILD_ID3             3         // Id of my 3rd sensor child
#define CHILD_ID4             4         // Id of my 4th sensor child

/* ************************************************************************************** */
/* These are use for local debug of code, hwDebugPrint is defined in MyHwATMega328.cpp */
#ifdef MY_DEBUG1
#define debug1(x,...) hwDebugPrint(x, ##__VA_ARGS__)
#else
#define debug1(x,...)
#endif

#ifdef MY_DEBUG2
#define debug2(x,...) hwDebugPrint(x, ##__VA_ARGS__)
#else
#define debug2(x,...)
#endif


/* ************************************************************************************** */
// All #define above need to be prior to the #include <MySensors.h> below
#include <MySensors.h>
/* ************************************************************************************** */


/* ************************************************************************************** */
#define PressPin      A4                            // Pressure sensor is on analog input, 0 to 100psi
#define BattVolt      A5                            // Battery Voltage on pin A5.  270k/270k divder = 1/2,  6.6v max

/* ************************************************************************************** */

MyMessage LEVEL1         (CHILD_ID1,V_LEVEL);       // 37 0x25      // Send Water Saturation Ch 1
MyMessage LEVEL2         (CHILD_ID2,V_LEVEL);       // 37 0x25      // Send Water Saturation Ch 2
MyMessage LEVEL3         (CHILD_ID3,V_LEVEL);       // 37 0x25      // Send Water Saturation Ch 3
MyMessage LEVEL4         (CHILD_ID4,V_LEVEL);       // 37 0x25      // Send Water Saturation Ch 4

MyMessage IMP1           (CHILD_ID1, V_IMPEDANCE);  // 14 0x0E      // Send Resistance Ch 1
MyMessage IMP2           (CHILD_ID2, V_IMPEDANCE);  // 14 0x0E      // Send Resistance Ch 2
MyMessage IMP3           (CHILD_ID3, V_IMPEDANCE);  // 14 0x0E      // Send Resistance Ch 3
MyMessage IMP4           (CHILD_ID4, V_IMPEDANCE);  // 14 0x0E      // Send Resistance Ch 4

MyMessage VBAT           (CHILD_ID1, V_VOLTAGE);    // 38 0x26      // Send Battery Voltage

MyMessage PressureMsg    (CHILD_ID1,V_PRESSURE);    // 04 0x04      // Send current Water Pressure
MyMessage TextMsg        (CHILD_ID1,V_TEXT);        // 47 0x2F      // Send status Messages
MyMessage HumMsg         (CHILD_ID1,V_HUM);         // 01 0x01      // Send current Humidity 
MyMessage TempMsg        (CHILD_ID1,V_TEMP);        // 00 0x00      // Send current Temperature

/* ************************************************************************************** */

#if defined MyDS3231
  DS3231 clock;
  RTCDateTime dt;
  boolean isAlarm = false;
  boolean alarmState = false;
#endif

/* *************************** Forward Declaration ************************************* */
void SendPressure();
void SendKeepAlive();
void getTempSi7021();
void getTempMCP9800 ();
void receive(const MyMessage &message);
void hexdump(unsigned char *buffer, unsigned long index, unsigned long width);
int  GetMoisture(unsigned long read);
void soilsensors();
void measureSensor();
void addReading(long resistance);
long average();

/* ************************************************************************************** */
unsigned long SEND_FREQUENCY        = 15000;      // Minimum time between send (in milliseconds). We don't want to spam the gateway.
unsigned long KEEPALIVE_FREQUENCY   = 120000;     // Send Keep Alive message at this rate

unsigned long currentTime         = 0;
unsigned long lastSendTime        = 0;
unsigned long keepaliveTime       = 0;
          
int pressure      = 0;                            // Current value from ATD
float PSI         = 0;                            // Current PSI
float PSI_CAL     = 2.0;                          // Calibration of sensor

int floatMSB      = 0;                            // used to convert float to int for printing
int floatR        = 0;

static float humi, temp;                          // used by Si7021, MCP9800, DS3231

/* ************************************************************************************** */

/* Pin assigments for Moisture Mux */
#define SensDY  15
#define SensDX  17
#define SensAY  A0  
#define SensAX  A2  
#define MuxA    8
#define MuxB    7
#define MuxINH  9

typedef struct {                  // Structure to be used in percentage and resistance values matrix to be filtered (have to be in pairs)
  int moisture;
  int resistance;
} values;

// Setting up format for reading 4 soil sensors
#define NUM_READS 12              // Number of sensor reads for filtering

const long knownResistor = 4700;  // Value of reference resistors in Ohms, = reference for sensor

unsigned long supplyVoltage;      // Measured supply voltage
unsigned long sensorVoltage;      // Measured sensor voltage
int zeroCalibration = 138;        // calibrate sensor resistace to zero when input is short circuited
                                  // basically this is compensating for the mux switch resistance

values valueOf[NUM_READS];        // Calculated  resistances to be averaged
long buffer[NUM_READS];
int index2;
int i;                            // Simple index variable
int j=0;                          // Simple index variable

long resistance = 0;
long moisture =   0;

unsigned long read1 = 0;
unsigned long read2 = 0;
unsigned long read3 = 0;
unsigned long read4 = 0;

 
/* **************************************************************************** */
/* ************************** Before ****************************************** */
/* **************************************************************************** */
 // Before is part of MySensor core 
void before() 
{ 
     debug1(PSTR("***In before***\n"));
 
 // need to set up pins prior to reading them...
     pinMode(ID0, INPUT_PULLUP);
     pinMode(ID1, INPUT_PULLUP);
     pinMode(ID2, INPUT_PULLUP);
     
     myNodeID  = !digitalRead (ID0);                     // ID bit are 0 = on, so we invert them
     myNodeID |= (!digitalRead(ID1) << 1);
     myNodeID |= (!digitalRead(ID2) << 2);
     myNodeID += NodeID_Base; 

    // Pin for onboard LED
    pinMode(OnBoardLed, OUTPUT);
    digitalWrite(OnBoardLed, LOW);
}


/* **************************************************************************** */
/* ************************** Setup ******************************************* */
/* **************************************************************************** */
void setup()  
{  
 //   wdt_enable(WDTO_8S);                // lets set WDT in case we have a problem...
      
    send(TextMsg.set("Starting"), AckFlag);  wait(SendDelay);   
    debug1(PSTR("***In Setup***\n"));
    //debug1(PSTR(" ** Hello from the Water Sensor on a M0 **\n") );
  
    // de-select on board Flash 
    pinMode(OnBoardFlash, OUTPUT);
    digitalWrite(OnBoardFlash, HIGH);

    // Pin for DS3231 alarm interrupt
    pinMode(38, INPUT_PULLUP);


/* ************************************************************************************** */
  const char compile_file[]  = __FILE__ ;
  debug1(PSTR(" %s %s\n"), SKETCHNAME, SKETCHVERSION);
  debug1(PSTR(" %s \n"), compile_file);
  
  const char compile_date[]  = __DATE__ ", " __TIME__;
  debug1(PSTR(" %s \n\n"), compile_date);
  debug1(PSTR(" My Node ID: %u\n\n"), myNodeID);
  
  // set up ATD and reference, for ATD to use:
  //analogReference(AR_EXTERNAL);
  //analogReference(AR_INT1V);
  //analogReference(AR_EXTERNAL);

      analogReadResolution(12);

#if defined Sensor_SI7021
    si7021.initialize();
#endif

#if defined Sensor_MCP9800
    MCP9800.writeConfig(ADC_RES_12BITS);       // max resolution, 0.0625 °C
#endif

#if defined MoistureSensor
  // setting up the sensor interface
  // initialize digital pins SensDX, SensDY as an high impedance input.
  // Pin SensDX,SensDY are for driving the soil moisture sensor
  pinMode(SensDX, INPUT);    
  pinMode(SensDY, INPUT);
   
  // Pin MuxINH is for enabling Mux switches
  pinMode(MuxINH, OUTPUT);

  // Pin MuxA,MuxB are for selecting sensor 1-4
  pinMode(MuxA, OUTPUT);  // Mux input A
  pinMode(MuxB, OUTPUT);  // Mux input B 
#endif 

/* ******** This setup the DS3231 and its alarms ********* */
#if defined MyDS3231
  clock.begin();
  clock.enableOutput(false);                    // set for interrupt
  
  // Disarm alarms and clear alarms for this example, because alarms is battery backed.
  // Under normal conditions, the settings should be reset after power and restart microcontroller.
  clock.armAlarm1(false);
  clock.armAlarm2(false);
  clock.clearAlarm1();
  clock.clearAlarm2();

  // Set sketch compiling time to the DS3231
  debug1(PSTR("Setting Time on DS3231 \n"));
  clock.setDateTime(__DATE__, __TIME__);

  // Set our alarms...
  // Set Alarm1 - At  10 second pass the minute
  // setAlarm1(Date or Day, Hour, Minute, Second, Mode, Armed = true)
  clock.setAlarm1(0, 0, 0, 10, DS3231_MATCH_S);

  // Set Alarm2 - At "myNodeID" minute past the hour      // this allow for pseudo-random TX
  // setAlarm2(Date or Day, Hour, Minute, Mode, Armed = true)
  clock.setAlarm2(0, 0, myNodeID, DS3231_MATCH_M);

  // Attach Interrput.  DS3231 INT is connected to Pin 38
  attachInterrupt(38, ClockAlarm, LOW);                   // please note that RISING and FALLING do NOT work on current Zero code base

#endif

} // end setup()


/* **************************************************************************** */
/* *********************** Presentation *************************************** */
/* **************************************************************************** */
void presentation()  
{
 // Send the sketch version information to the gateway and Controller
  sendSketchInfo(SKETCHNAME, SKETCHVERSION, AckFlag);   wait(SendDelay);
 
  // Register this device as Water Moisture Sensor
  present(CHILD_ID1, S_MOISTURE, "Water Moisture", AckFlag); wait(SendDelay);
 }


/* **************** DS3231 Alarm ******************* */
#if defined MyDS3231
void ClockAlarm()
{
  if (clock.isAlarm1(false))      // is set to true, will also clear the alarm..
  {
    clock.clearAlarm1();
    //debug1(PSTR("*** Alarm 1 ***\n"));
  }

  if (clock.isAlarm2(false))
  {
    clock.clearAlarm2();
    //debug1(PSTR("*** Alarm 2 ***\n"));
  }
}
#endif


/* ***************** Send Pressure ***************** */
void SendPressure()
{
#ifdef WaterPressure
/* We will read the analog input from the presure transducer 
 *  and convert it from an analog voltage to a pressure in PSI
 * 
 *  Output: 0.5V – 4.5V linear voltage output. 0 psi outputs 0.5V, 50 psi outputs 2.5V, 100 psi outputs 4.5V 
 *  0   psi = .33v after scalling 5.0v to 3.3v
 *  50  psi = 1.65v
 *  100 psi = 2.97v
 *
 *  3.3v/1023 = .003225806 volt per bit or 3.3V/4095 = .000805861 volts per bit
 */
    pressure  = analogRead    (PressPin) ;                    // this is a junk read to clear ATD
    wait(25);
 
    pressure  = analogRead    (PressPin) ;

    if (pressure < 106) pressure = 106;                       // we have a minimum of .5v = 0 PSI
    PSI = (pressure - 106 ) * .1246;                          // where did we get this?? was .119904
    PSI = PSI + PSI_CAL;                                      // calibration adjustment if needed
    
    floatMSB = PSI * 100;
    floatR = floatMSB % 100;
    debug1(PSTR("PSI:  %0u.%02u\n"), floatMSB/100, floatR);

    send(PressureMsg.set(PSI, 2), AckFlag);  wait(SendDelay); // Send water pressure to gateway 
#endif  
}


/* ***************** Send Si7021 Temp & Humidity ***************** */
void getTempSi7021()
{
#if defined Sensor_SI7021
      si7021.triggerMeasurement();
      wait (25);
      si7021.getHumidity    (humi);
      si7021.getTemperature (temp);
      
      temp = (temp * 1.8) + 32.0;                                // to get deg F

      floatMSB = humi * 100;                                     // we donot have floating point printing in debug print
      floatR = floatMSB % 100; 
      debug1(PSTR("Humi: %0u.%02u%% \n"), floatMSB/100, floatR);
      
      send(HumMsg.set(humi, 2), AckFlag);  wait(SendDelay);

      floatMSB = temp * 100;                                     // we donot have floating point printing in debug print
      floatR = floatMSB % 100; 
      debug1(PSTR("Temp Si: %0u.%02uF \n"), floatMSB/100, floatR);
      
      send(TempMsg.set(temp, 2), AckFlag);  wait(SendDelay);
#endif
}


/* ***************** Send MCP9800 Temp ***************** */
void  getTempMCP9800 ()
{
#if defined Sensor_MCP9800
 
//    temp = MCP9800.readTempC16(AMBIENT) / 16.0;               // In deg C
      temp = MCP9800.readTempF10(AMBIENT) / 10.0;               // In deg F
      
      floatMSB = temp * 100;                                     // we donot have floating point printing in debug print
      floatR = floatMSB % 100; 
      debug1(PSTR("Temp MCP: %0u.%02uF \n"), floatMSB/100, floatR);
      
      send(TempMsg.set(temp, 2), AckFlag);  wait(SendDelay);
#endif
}

/* ***************** Send DS3231 Temp ***************** */
void getTempDS3231()
{
     #if defined  Sensor_MCP9800 || defined Sensor_SI7021       // we will used other Temp sensor if avilable
        // nothing here
     #elif defined MyDS3231
      clock.forceConversion();                                  // Start conversion of Temp sensor
      wait(25);
      temp = clock.readTemperature();
      temp = (temp * 1.8) + 32.0;                                // to get deg F
      floatMSB = temp * 100;                                     // we donot have floating point printing in debug print
      floatR = floatMSB % 100; 
      debug1(PSTR("Temp DS: %0u.%02uF \n"), floatMSB/100, floatR);
        
      send(TempMsg.set(temp, 2), AckFlag);  wait(SendDelay);

     #endif
}


/* ***************** Send Keep Alive ***************** */
void SendKeepAlive()
{
  if (currentTime - keepaliveTime >= KEEPALIVE_FREQUENCY)
    {
          debug1(PSTR("***Sending Heart Beat\n"));
          sendHeartbeat();  wait(SendDelay);

          SendPressure();                                               // send water pressure to GW if we have it
          getTempSi7021();
          getTempMCP9800();
          getTempDS3231();
          keepaliveTime = currentTime;                                  // reset timer
    }  
}


/* **************************************************************************** */
/* **************************** Loop ****************************************** */
/* **************************************************************************** */
void loop()     
{ 
  //wdt_reset();
  currentTime = millis();                                       // get the current time

 /* ***************** Send  ***************** */
  if (currentTime - lastSendTime >= SEND_FREQUENCY)             // Only send values at a maximum rate  
    {
      lastSendTime = currentTime; 
      
      soilsensors(); 
      int vbat = analogRead(5);
      float Vsys =  vbat * 0.000805861 * 1.97;                  // read the battery voltage, 12bits = 0 -> 4095, divider is 1/2
      send(VBAT.set(Vsys, 2), AckFlag);  wait(SendDelay);
      sendBatteryLevel(vbat/41);  wait (SendDelay);             // Sent MySensor battery in %, count / 41 = 4095/41 = 99%
      floatMSB = Vsys * 100;                                    // we donot have floating point printing in debug print
      floatR = floatMSB % 100; 
      debug1(PSTR("Vbat: %0u.%02uV \n"), floatMSB/100, floatR);
    
    }
     SendKeepAlive(); 

  #ifdef MyDS3231
    //LowPower.standby();
    //LowPower.idle(IDLE_2); 
  #endif
                       
}   // end of loop


/****************** Message Receive Loop *******************
 * 
 * This is the message receive loop, here we look for messages address to us 
 * 
 * ******************************************************* */
void receive(const MyMessage &message) 
{
   //debug2(PSTR("Received message from gw:\n"));
   //debug2(PSTR("Last: %u, Sender: %u, Dest: %u, Type: %u, Sensor: %u\n"), message.last, message.sender, message.destination, message.type, message.sensor);
   
// Make sure its for our child ID
  if (message.sensor == CHILD_ID1 )
  {
    if  (message.type==V_VAR1)                                                   // Update Pulse Count to new value
      {
        debug2(PSTR("Received last pulse count from gw: %u\n"), pulseCount);
      }
    
     if ( message.type==V_VAR2)                                                  // ??
      {
        debug2(PSTR("Received V_VAR2 message from gw: %u\n"), pulseCount );
      }
    
     if ( message.type==V_VAR3)                                                   // send all values now
      {
        SendKeepAlive();        
        debug2(PSTR("Received V_VAR3 message from gw"));
      }

     if ( message.type==V_VAR4)                                                   // Calabrate offset for PSI
      {
        debug2(PSTR("Received V_VAR4 message from gw: %u\n"), PSI_CAL);
      }

    }  // end if (message.sensor == CHILD_ID1 )


 // Check for any messages for child ID = 2
  if (message.sensor == CHILD_ID2 )
    {
      debug2(PSTR("Received Child ID-2 message from gw. \n"));
    }
}


/* ******************************************************** */
void soilsensors()
{
// Select sensor 1, and enable MUX
  digitalWrite(MuxA, LOW); 
  digitalWrite(MuxB, LOW); 
  digitalWrite(MuxINH, LOW); 
  measureSensor();
  read1 = average();

    moisture = GetMoisture(read1);
    debug1(PSTR("Moisture 1: %u Res: %u\n"), moisture, read1);
    send(LEVEL1.set(moisture), AckFlag);  wait(SendDelay);
    send(IMP1.set(read1), AckFlag);  wait(SendDelay);

// Select sensor 2, and enable MUX
  digitalWrite(MuxA, LOW); 
  digitalWrite(MuxB, HIGH); 
  digitalWrite(MuxINH, LOW); 
  measureSensor();
  read2 = average();

    moisture = GetMoisture(read2);
    debug1(PSTR("Moisture 2: %u Res: %u\n"), moisture, read2);
    send(LEVEL2.set(moisture), AckFlag);  wait(SendDelay);
    send(IMP2.set(read2), AckFlag);  wait(SendDelay);

    // Select sensor 3, and enable MUX
  digitalWrite(MuxA, HIGH); 
  digitalWrite(MuxB, LOW); 
  digitalWrite(MuxINH, LOW); 
  measureSensor();
  read3 = average();

    moisture = GetMoisture(read3);
    debug1(PSTR("Moisture 3: %u Res: %u\n"), moisture, read3);
    send(LEVEL3.set(moisture), AckFlag);  wait(SendDelay);
    send(IMP3.set(read3), AckFlag);  wait(SendDelay);


  // Select sensor 4, and enable MUX
  digitalWrite(MuxA, HIGH); 
  digitalWrite(MuxB, HIGH); 
  digitalWrite(MuxINH, LOW); 
  measureSensor();
  read4 = average();

    moisture = GetMoisture(read4);
    debug1(PSTR("Moisture 4: %u Res: %u\n"), moisture, read4);
    send(LEVEL4.set(moisture), AckFlag);  wait(SendDelay);
    send(IMP4.set(read4), AckFlag);  wait(SendDelay);

  digitalWrite(MuxINH, HIGH);             // Disable Mux, this will isolate CPU from Sensor

  return;
}

/* ******************************************************** */ 
int GetMoisture(unsigned long read)
{
    int moisture = min( int( pow( read / 31.65 , 1.0 / -1.695 ) * 400 + 0.5 ) , 100 );
    if (moisture <= 0) moisture = 0;
    if (moisture > 100) moisture = 100;
  return moisture;
}


/* ******************************************************** */
void measureSensor()
{
  for (i=0; i< NUM_READS; i++) 
      {
        pinMode(SensDX, OUTPUT);              // this will be used as current source
        pinMode(SensDY, INPUT);               // we will read from this channel 
        digitalWrite(SensDX, LOW);            // clear any stray current  
        digitalWrite(SensDX, HIGH);           // set it high to supply current to sensor 
        delayMicroseconds(250);
        sensorVoltage = analogRead(SensAY);   // read the sensor voltage
        supplyVoltage = analogRead(SensAX);   // read the supply voltage
               
        digitalWrite(SensDX, LOW);            // clear any stray current  
        pinMode(SensDX, INPUT);               // HiZ the input channel
        pinMode(SensDY, INPUT);               // HiZ the input channel
        resistance = (knownResistor * (supplyVoltage - sensorVoltage ) / sensorVoltage) - zeroCalibration ;
        if (resistance <= 0) resistance = 0;            // do some reasonable bounds checking
        if (resistance >= 100000) resistance = 100000;  // 100k max
             
        addReading(resistance);               // save it
        delayMicroseconds(250);
        
    // Invert the current and do it again...
        pinMode(SensDY, OUTPUT);              // this will be used as current source
        pinMode(SensDX, INPUT);               // we will read from this channel  
        digitalWrite(SensDY, LOW);            // clear any stray current    
        digitalWrite(SensDY, HIGH);           // set it high to supply current to sensor  
        delayMicroseconds(250);
        sensorVoltage = analogRead(SensAX);   // read the sensor voltage
        supplyVoltage = analogRead(SensAY);   // read the supply voltage
        
        digitalWrite(SensDY, LOW);            // clear any stray current 
        pinMode(SensDY, INPUT);               // HiZ the input channel
        pinMode(SensDX, INPUT);               // HiZ the input channel
        
        resistance = (knownResistor * (supplyVoltage - sensorVoltage ) / sensorVoltage) - zeroCalibration ;
        if (resistance <= 0) resistance = 0;            // do some reasonable bounds checking
        if (resistance >= 100000) resistance = 100000;  // 100k max
        
       addReading(resistance);                // save it
       delay(100);
      } 
}

/* ******************************************************** */
// Averaging algorithm
void addReading(long resistance)
{
    buffer[index2] = resistance;
    index2++;
    if (index2 >= NUM_READS) index2 = 0;
}

/* ******************************************************** */
  long average()
  {
    long sum = 0;
    for (int i = 0; i < NUM_READS; i++)
    {
      sum += buffer[i];
    }
    return (long)(sum / NUM_READS);
}


/* *********************** The End ************************ */
/* ******************************************************** */ 

