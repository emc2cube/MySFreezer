/**
   The MySensors Arduino library handles the wireless radio link and protocol
   between your home built sensors/actuators and HA controller of choice.
   The sensors forms a self healing radio network with optional repeaters. Each
   repeater and gateway builds a routing tables in EEPROM which keeps track of the
   network topology allowing messages to be routed to nodes.

   Created by Henrik Ekblad <henrik.ekblad@mysensors.org>
   Copyright (C) 2013-2015 Sensnology AB
   Full contributor list: https://github.com/mysensors/Arduino/graphs/contributors

   Documentation: http://www.mysensors.org
   Support Forum: http://forum.mysensors.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   version 2 as published by the Free Software Foundation.

*/

// Customize your device name. You should use a pattern with 3 words separated by spaces "Room Nominal_Temperature Equipment"
// ex: "Lab -20°C Freezer" or "Hallway -80°C Freezer" or "TC 37°C Incubator".
String Location = "Lab -20°C Freezer";     // Location of the device, will be sent to the gateway with each device

// Enable debug prints
//#define MY_DEBUG

// Enable and select radio type attached
#define MY_RADIO_NRF24
//#define MY_RADIO_RFM69

// RF24 settings
// Use custom RF24 channel (default 76)
#define MY_RF24_CHANNEL AUTO

//#define ALARM_MONITORING_HW                 // Enable alarm monitoring using interrupt. Connect to COM (commmon) and NO (normally open) pins
#define ALARM_MONITORING_SW                 // Enable software alarm, will be triggered by temperature
#define TEMPERATURE_ALARM -10               // If measured temperature is higher, alarm will activate. Required for ALARM_MONITORING_SW
#define TEMPERATURE_MONITORING              // Enable temperature monitoring, require one of the sensors to be activated
//#define MA_SENSOR                           // Enable temperature monitoring using a 4-40ma output (usually available on -80ºC Freezers) using the An+ and An- ports
#define DALLAS_SENSOR                       // Enable temperature monitoring using a DS18b20 probe. Use 3V3, 1Wire and GND ports
//#define THERMISTOR_SENSOR                   // Enable temperature monitoring using a thermistor probe. Thermistor should be wired between GND and the 1Wire ports.
#define BATTERY_MONITORING                  // Enable battery monitoring

// Set this to true if you want to send values altough the values did not change.
// This is only recommended when not running on batteries.
const bool SEND_ALWAYS = true;

#include <SPI.h>
#include <MySensors.h>

#if defined ALARM_MONITORING_HW
#define CHILD_ID_ALARM 0
#define ALARM_SENSOR 3                      // The digital input you attached your motion sensor.  (Only 2 and 3 generates interrupt!)
#define INTERRUPT ALARM_SENSOR-2            // Usually the interrupt = pin -2 (on uno/nano anyway)
MyMessage msgAlarm(CHILD_ID_ALARM, V_STATUS);
boolean alarm, lastAlarm;
#endif

#if defined ALARM_MONITORING_SW
#define CHILD_ID_ALARM 0
MyMessage msgAlarm(CHILD_ID_ALARM, V_STATUS);
boolean alarm, lastAlarm;
#endif

#if defined TEMPERATURE_MONITORING
#define CHILD_ID_TEMP 1
MyMessage msgTemp(CHILD_ID_TEMP, V_TEMP);
float temperature, lastTemp;

#if defined MA_SENSOR
#define MA_SENSOR_PIN A2                    // Analog pin for 4-20mA sensor, A2
#endif

#if defined DALLAS_SENSOR
#define ONE_WIRE_BUS 7                     // 1Wire pin, D7
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature dallas(&oneWire);
int numSensors = 0;
#endif // end DALLAS_SENSOR

#if defined THERMISTOR_SENSOR
const int THERMISTOR_PIN = A1;              // Analog pin for thermistor, 1Wire pin, A1
#define THERMISTORNOMINAL 10000             // resistance at 25ºC
#define TEMPERATURENOMINAL 25               // temp. for nominal resistance (almost always 25ºC)
#define NUMSAMPLES 5                        // how many samples to take and average
#define BCOEFFICIENT 3950                   // The beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 4700                 // We are using the 1Wire pullup 4.7kΩ as reference
int samples[NUMSAMPLES];
#endif // end THERMISTOR_SENSOR

#if defined MY_DEBUG
#define CHILD_ID_DEBUG 100
MyMessage msgDebug(CHILD_ID_DEBUG, V_CURRENT);
#endif // end TEMPERATURE_MONITORING + MY_DEBUG

#endif // end TEMPERATURE_MONITORING

#if defined BATTERY_MONITORING
const int  BATTERY_SENSE_PIN = A0;  // select the input pin for the battery sense point
int oldBatteryPcnt = 0;
#endif // end BATTERY_MONITORING

#define SN "MySFreezer"                     // Name of the sketch
#define SV "2.0"                            // Version (2.0 : use MySensors 2.0)

#define MESSAGEWAIT 500                     // Wait a few ms between radio Tx

unsigned long SLEEP_TIME = 1800000UL;       // Sleep time between reads when alarm is off (in milliseconds)
unsigned long SLEEP_TIME_ALARM = 600000UL;  // Sleep time between reads when alarm is on (in milliseconds)
boolean metric = true;

void setup() {

#if defined DALLAS_SENSOR
  // Start up Dallas temperature probes
  dallas.begin();
  dallas.setWaitForConversion(false);
  numSensors = dallas.getDeviceCount();
#endif

#if defined MY_DEBUG
  // If debug, TX every 30s on idle / 10s on alarm
  SLEEP_TIME = 30000UL;                     // Sleep time between reads when alarm is off (30s)
  SLEEP_TIME_ALARM = 10000UL;               // Sleep time between reads when alarm is on (10s)
#endif

  metric = getConfig().isMetric;

  // use the 1.1 V internal reference
#if defined(__AVR_ATmega2560__)
  analogReference(INTERNAL1V1);
#else
  analogReference(INTERNAL);
#endif

#if defined THERMISTOR_SENSOR
  pinMode(THERMISTOR_PIN, INPUT);
#endif
#if defined MA_SENSOR
  pinMode(MA_SENSOR_PIN, INPUT);
#endif
#if defined ALARM_MONITORING_HW
  pinMode(ALARM_SENSOR, INPUT_PULLUP);      // set the digital pin as input with internal pull-up resistor
#endif
  Serial.println(F("Node running"));
}

void presentation()
{
  // Send the Sketch Version Information to the Gateway
  wait(MESSAGEWAIT);
  sendSketchInfo(SN, SV);

  // Register all sensors to gw (they will be created as child devices)
#if (defined ALARM_MONITORING_HW || defined ALARM_MONITORING_SW)
  String alarmReg = Location + " Alarm";
  wait(MESSAGEWAIT);
  present(CHILD_ID_ALARM, S_BINARY, alarmReg.c_str());
#endif

#if defined TEMPERATURE_MONITORING
#if defined DALLAS_SENSOR
  numSensors = dallas.getDeviceCount();
  // Present all sensors to controller
  for (int i = CHILD_ID_TEMP; i < numSensors + CHILD_ID_TEMP; i++) {
    String dallasReg = Location + " Temp " + String(i - CHILD_ID_TEMP + 1);
    wait(MESSAGEWAIT);
    present(i, S_TEMP, dallasReg.c_str());
  }
#else
  String tempReg = Location + " Temp";
  wait(MESSAGEWAIT);
  present(CHILD_ID_TEMP, S_TEMP, tempReg.c_str());
#endif // end DALLAS_SENSOR
#if defined MY_DEBUG
  String debugReg = Location + " Debug";
  wait(MESSAGEWAIT);
  present(CHILD_ID_DEBUG, S_MULTIMETER, debugReg.c_str());
#endif // end TEMPERATURE_MONITORING + MY_DEBUG
#endif // end TEMPERATURE_MONITORING
}

void loop()
{

#if defined TEMPERATURE_MONITORING
  // Get temperature from various sensors
#if defined MA_SENSOR
  int voltage = analogRead(MA_SENSOR_PIN);
  wait(MESSAGEWAIT);
  voltage = analogRead(MA_SENSOR_PIN);
  temperature = map(voltage, 225, 875, -100, 50);
#if defined MY_DEBUG
  wait(MESSAGEWAIT);
  send(msgDebug.set(voltage, 0));
  Serial.print(F("Voltage ADC: "));
  Serial.println(voltage);
#endif
  if (SEND_ALWAYS || temperature != lastTemp) {
    lastTemp = temperature;
    if (!metric) {
      temperature = temperature * 9.0 / 5.0 + 32.0;
    }
    wait(MESSAGEWAIT);
    send(msgTemp.set(temperature, 1));
  }
#if defined MY_DEBUG
  Serial.print(F("Temperature: "));
  Serial.print(temperature);
  Serial.println(metric ? F(" °C") : F(" °F"));
#endif
#endif // end MA_SENSOR

#if defined DALLAS_SENSOR
#if defined MY_DEBUG
  Serial.print("Measuring  temperatures...");
#endif
  dallas.requestTemperatures(); // Send the command to get temperatures
#if defined MY_DEBUG
  Serial.println(" DONE");
#endif
  for (int i = 0; i < numSensors; i++) {
    // Fetch and round temperature to one decimal
    temperature = static_cast<float>(static_cast<int>((getConfig().isMetric ? dallas.getTempCByIndex(i) : dallas.getTempFByIndex(i)) * 10.)) / 10.;
    if (temperature != -127.00 && temperature != 85.00) {
      // Send in the new temperature
      if (SEND_ALWAYS || temperature != lastTemp) {
        lastTemp = temperature;
        wait(MESSAGEWAIT);
        send(msgTemp.setSensor(CHILD_ID_TEMP + i).set(temperature, 1));
      }
#if defined MY_DEBUG
      // After we got the temperatures, we can print them.
      // We use the function ByIndex, and as an example get the temperature from the first sensor only.
      Serial.print("Temperature for the device ");
      Serial.print(i);
      Serial.print(" is: ");
      Serial.print(temperature);
      Serial.println(getConfig().isMetric ? "C" : "F");
#endif
    }
  }
#endif // end DALLAS_SENSOR

#if defined THERMISTOR_SENSOR
  uint8_t i;
  float voltageR;
  for (i = 0; i < NUMSAMPLES; i++) {
    samples[i] = analogRead(THERMISTOR_PIN);
    wait(10);
  }
  // average all the samples out
  voltageR = 0;
  for (i = 0; i < NUMSAMPLES; i++) {
    voltageR += samples[i];
  }
  voltageR /= NUMSAMPLES;
#if defined MY_DEBUG
  wait(MESSAGEWAIT);
  send(msgDebug.set(voltageR, 0));
  Serial.print(F("Freezer voltage ADC: "));
  Serial.println(voltageR);
#endif
  temperature = (1 / ((log(voltageR / THERMISTORNOMINAL) / BCOEFFICIENT) + (1.0 / (TEMPERATURENOMINAL + 273.15)))) - 273.15;
  if (SEND_ALWAYS || temperature != lastTemp) {
    lastTemp = temperature;
    if (!metric) {
      temperature = temperature * 9.0 / 5.0 + 32.0;
    }
    wait(MESSAGEWAIT);
    send(msgTemp.set(temperature, 1));
  }
#if defined MY_DEBUG
  Serial.print(F("Temperature: "));
  Serial.print(temperature);
  Serial.println(metric ? F(" °C") : F(" °F"));
#endif
#endif // end THERMISTOR_SENSOR

#endif // end TEMPERATURE_MONITORING


#if (defined ALARM_MONITORING_HW || defined ALARM_MONITORING_SW)
#if defined ALARM_MONITORING_HW
  // Read alarm state
  boolean alarm = digitalRead(ALARM_SENSOR) == LOW;
#if defined MY_DEBUG
  Serial.print("Alarm: ");
  Serial.println(alarm ? "ON" : "OFF");
#endif
#endif
#if defined ALARM_MONITORING_SW
  // Define alarm state
  boolean alarm = temperature > TEMPERATURE_ALARM;
#if defined MY_DEBUG
  Serial.print("Alarm is ");
  Serial.print(alarm ? "ON ( " : "OFF ( ");
  Serial.print(temperature);
  Serial.print(getConfig().isMetric ? "C" : "F");
  Serial.print(alarm ? " > " : " ≤ ");
  Serial.print(TEMPERATURE_ALARM);
  Serial.println(getConfig().isMetric ? "C )" : "F )");
  Serial.println("");
#endif
#endif
  if (alarm != lastAlarm) {
    lastAlarm = alarm;
    wait(MESSAGEWAIT);
    send(msgAlarm.set(alarm ? "1" : "0")); // Send tripped value to gw
  }
#endif // end ALARM_MONITORING


#if defined BATTERY_MONITORING
  // get the battery Voltage
  int sensorValue = analogRead(BATTERY_SENSE_PIN);
  wait(MESSAGEWAIT);
  sensorValue = analogRead(BATTERY_SENSE_PIN);
#ifdef MY_DEBUG
  Serial.print(F("Battery voltage ADC: "));
  Serial.println(sensorValue);
#endif
  // 1M, 470K divider across battery and using internal ADC ref of 1.1V
  // Sense point is bypassed with 0.1 uF cap to reduce noise at that point
  // ((1e6+470e3)/470e3)*1.1 = Vmax = 3.44 Volts
  // 3.44/1023 = Volts per bit = 0.003363075
  int batteryPcnt = sensorValue / 10;
  batteryPcnt = batteryPcnt > 98 ? 100 : batteryPcnt;
#ifdef MY_DEBUG
  float batteryV  = sensorValue * 0.003363075;
  Serial.print("Battery voltage: ");
  Serial.print(batteryV);
  Serial.println(" V");
  Serial.print("Battery percent: ");
  Serial.print(batteryPcnt);
  Serial.println(" %");
#endif
  if (abs(batteryPcnt - oldBatteryPcnt) <= 1) {
    batteryPcnt = oldBatteryPcnt;
  }
  if (oldBatteryPcnt != batteryPcnt) {
    wait(MESSAGEWAIT);
    sendBatteryLevel(batteryPcnt);
    oldBatteryPcnt = batteryPcnt;
  }
#endif // end BATTERY_MONITORING

#if (defined ALARM_MONITORING_HW)
  sleep(INTERRUPT, CHANGE, alarm ? SLEEP_TIME_ALARM : SLEEP_TIME); // Sleep for SLEEP_TIME if alarm is off, SLEEP_TIME_ALARM if alarm is on (more reads to monitor emergency situations) or until interrupt
#else
  sleep(alarm ? SLEEP_TIME_ALARM : SLEEP_TIME); // Sleep for SLEEP_TIME if alarm is off, SLEEP_TIME_ALARM if alarm is on (more reads to monitor emergency situations)
#endif
}
