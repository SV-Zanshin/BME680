/*! @file SDLoggerSPIDemo.ino

@section SDLoggerSPIDemo_intro_section Description

Example program for using SPI to set and read the Bosch BME680 sensor and log readings to an SD-Card. The sensor 
measures temperature, pressure and humidity and is described at https://www.bosch-sensortec.com/bst/products/all_products/BME680.
The datasheet is available from Bosch at https://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BME680_DS001-11.pdf \n\n

The most recent version of the BME680 library is available at https://github.com/SV-Zanshin/BME680 and the 
documentation of the library as well as example programs are described in the project's wiki pages located at 
https://github.com/SV-Zanshin/BME680/wiki. \n\n

The BME680 is an extremely small physical package that is so tiny as to be impossible to solder at home, hence it 
will be used as part of a third-party breakout board. There are several such boards available at this time, for 
example \n
Company  | Link
-------  | ----------
Sparkfun | https://www.sparkfun.com/products/14570
BlueDot  | https://www.bluedot.space/sensor-boards/bme680/
Adafruit | https://learn.adafruit.com/adafruit-BME680-humidity-barometric-pressure-temperature-sensor-breakout \n\n

Bosch supplies sample software that runs on various platforms, including the Arduino family; this can be downloaed
at https://github.com/BoschSensortec/BSEC-Arduino-library . This software is part of the Bosch "BSEC" (Bosch 
Sensortec Environmental Cluster) framework and somewhat bulky and unwieldy for typical Arduino applications, hence
the choice to make a more compact and rather less abstract library. 
 
This example program initializes the BME680 to use SPI for communications. The library does not using floating
point numbers to save on memory space and computation time. The values for Temperature, Pressure and Humidity are
returned in deci-units, e.g. a Temperature reading of "2731" means "27.31" degrees Celsius. The display in the 
example program uses floating point for demonstration purposes only.  Note that the temperature reading is 
generally higher than the ambient temperature due to die and PCB temperature and self-heating of the element.\n\n

The pressure reading needs to be adjusted for altitude to get the adjusted pressure reading. There are numerous
sources on the internet for formulae converting from standard sea-level pressure to altitude, see the data sheet
for the BME180 on page 16 of http://www.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf. Rather than put a
floating-point function in the library which may not be used but which would use space, an example altitude
computation function has been added to this example program to show how it might be done.

@section SDLoggerSPIDemolicense License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
option) any later version. This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details. You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

@section SDLoggerSPIDemoauthor Author

Written by Arnd\@SV-Zanshin

@section SDLoggerSPIDemoversions Changelog

Version | Date       | Developer                     | Comments
------- | ---------- | ----------------------------- | -------------------------------------------------
1.0.0b  | 2020-05-22 | https://github.com/SV-Zanshin | Cloned from original SPIDemo program and modified
*/
#include "Zanshin_BME680.h" // Include the BME680 Sensor library
#include <SPI.h>            // Include the SPI standard library (it is also included in the BME680 library)    
#include <SD.h>             // Include the SD Card standard library

/*******************************************************************************************************************
** Declare all program constants                                                                                  **
*******************************************************************************************************************/
const uint8_t   BME_680_SPI_CS_PIN =            SS; ///< Use the standard SS pin for the BME680
const uint8_t   SD_CARD_SPI_CS_PIN =            24; ///< Use Pin A6 for the SD Card
const uint32_t  SERIAL_SPEED       =        115200; ///< Set the baud rate for Serial I/O
const uint8_t   NUMBER_READINGS    =            10; ///< Number of readings to average
const uint32_t  LONG_DELAY         =         10000; ///< Long delay in milliseconds - 10 seconds
const uint32_t  SHORT_DELAY        =          1000; ///< Long delay in milliseconds -  1 second
const uint32_t  FAST_MODE_DURATION =         60000; ///< How long to run detailed measurements after trigger
const char*     FILE_NAME          = "BME_680.csv"; ///< Filename on SD-Card
const uint16_t  TEMPERATURE_TRIP   =           100; ///< Trigger if delta is more than this deci-degrees
const uint16_t  PRESSURE_TRIP      =           100; ///< Trigger if delta is more than this Pascal
const uint16_t  HUMIDITY_TRIP      =          1000; ///< Trigger if delta is more than this milli-percent

/*******************************************************************************************************************
** Declare global variables and instantiate classes                                                               **
*******************************************************************************************************************/
BME680_Class BME680;                                ///< Create an instance of the BME680 class
File         dataFile;                              ///< Class for a SD-Card file
/*! @struct Structure to contain a group of readings */
struct reading
{
  int32_t temperature;                              ///< temperature in deci-degrees
  int32_t humidity;                                 ///< humidity in milli-percent
  int32_t pressure;                                 ///< pressure in Pascal
}; // of structure reading
uint8_t  idx               = 0;                     ///< Index into "data" structure
uint16_t loopCounter       = 0;                     ///< Loop counter for displaying iterations
uint32_t fastModeEndMillis = 0;                     ///< Millis value when fast mode stops
reading  data[NUMBER_READINGS];                     ///< Structure to hold accumulated measurements
int32_t  unused_gas;                                ///< Unused variable to hold (nonexistant) gas measurements
char     buf[32];                                   ///< Text buffer for sprintf() function
int32_t  avg_temperature;                           ///< Holds computed average over NUMBER_READINGS 
int32_t  avg_humidity;                              ///< Holds computed average over NUMBER_READINGS 
int32_t  avg_pressure;                              ///< Holds computed average over NUMBER_READINGS 
uint32_t fast_mode_end = 0;                         ///< Holds the millis() value when fast_mode ends

void normalMode()
{
  /*!
  @brief    Called to set the BME680 configuration values to those for normal (less accurate) readings
  @details  The configuration parameters are set to use minimal time (and energy) at the price of accuracy.
  @return   void
  */
  BME680.setOversampling(TemperatureSensor, Oversample2); // Average 2 readings for temperature
  BME680.setOversampling(HumiditySensor,    Oversample2); // Average 2 readings for humidity
  BME680.setOversampling(PressureSensor,    Oversample1); // Don't average readings for pressure
  BME680.setIIRFilter(IIR2);                              // Use IIR Filter of 2
  BME680.setGas(0,0);                                     // Setting either value to 0 turns off gas measurements
} // of method "normalMode()"
void accurateMode()
{
  /*!
  @brief    Called to set the BME680 configuration values to those for accurate readings
  @details  The configuration parameters are set to get more accurate measurements at the price of both additional
            time and energy.
  @return   void
  */
  BME680.setOversampling(TemperatureSensor, Oversample8); // Average 8 readings for temperature
  BME680.setOversampling(HumiditySensor,    Oversample8); // Average 8 readings for humidity
  BME680.setOversampling(PressureSensor,    Oversample8); // Average 8 readings for pressure
  BME680.setIIRFilter(IIR8);                              // Use IIR Filter of 8
  BME680.setGas(0,0);                                     // Setting either value to 0 turns off gas measurements
} // of method "accurateMode()"
void setup()
{
  /*!
  @brief    Arduino method called once at startup to initialize the system
  @details  This is an Arduino IDE method which is called first upon boot or restart. It is only called one time
            and then control goes to the main "loop()" method, from which control never returns
  @return   void
  */
  pinMode(BME_680_SPI_CS_PIN, OUTPUT);              // Declare the Chip-Select pin for the BME680 as output
  pinMode(SD_CARD_SPI_CS_PIN, OUTPUT);              // Declare the Chip-Select pin for the SD Card as output
  digitalWrite(SD_CARD_SPI_CS_PIN, HIGH);           // Write a high value to it in order to deselect device
  digitalWrite(BME_680_SPI_CS_PIN, HIGH);           // Write a high value to it in order to deselect device
  Serial.begin(SERIAL_SPEED);                       // Start serial port at Baud rate
  #ifdef  __AVR_ATmega32U4__                        // If this is a 32U4 processor, 
    delay(3000);                                    // then wait 3 seconds to initialize USB port
  #endif                                            
  Serial.print(F("Starting SDLoggerSPIDemo example program for BME680\n- Initializing BME680 sensor\n"));
  while (!BME680.begin(BME_680_SPI_CS_PIN))         // Start BME680 using hardware SPI protocol
  {
    Serial.print(F("-  Unable to find BME680. Trying again in 5 seconds.\n"));
    delay(5000);
  } // of loop until device is located
  normalMode();
  BME680.getSensorData(data[idx].temperature, data[idx].humidity, data[idx].pressure, unused_gas);
  Serial.print(F("- Setting BME680 to \"normal\" mode\n- Averaging over "));
  Serial.print(NUMBER_READINGS);
  Serial.print(F(" readings\n\nStarting SD-Card.\n"));
  while (!SD.begin(SD_CARD_SPI_CS_PIN)) // Start card using hardware SPI protocol
  {
    Serial.print(F("-  Unable to find SD Card. Trying again in 5 seconds.\n"));
    delay(5000);
  } // of loop until device is located
  Serial.print(F("- SD-Card Initialized\n"));
  dataFile = SD.open(FILE_NAME, FILE_WRITE);        // Open the logfile for writing and position to end-of-file
  if (!dataFile)
  {
    Serial.print(F("Unable to open file \""));
    Serial.print(FILE_NAME);
    Serial.print(F("\" on SD-Card. Error. Stopping."));
    while (1); // loop forever, don't continue
  } // if-then the file could be opened
  Serial.print(F("- File \""));
  Serial.print(FILE_NAME);
  Serial.print(F("\" successfully opened. Appending data.\n\n"));
  dataFile.print("Seconds,Temperature,Humidity,Pressure");
  for (uint8_t i = 1; i < NUMBER_READINGS; i++)     // fill complete array with initial reading values
  {
    data[i].temperature = data[0].temperature;
    data[i].humidity    = data[0].humidity;
    data[i].pressure    = data[0].pressure;
  } // of for-next each array element
} // of method setup()
void loop() 
{
  /*!
  @brief    Arduino method for the main program loop
  @details  This is the main program for the Arduino IDE, it is an infinite loop and keeps on repeating. 
            The "sprintf()" function is to pretty-print the values, since floating point is not supported on the 
            Arduino, split the values into those before and those after the decimal point.
  @return   void
  */
  if (loopCounter % 25 == 0)                                                             // Header every 25 loops
  {                                                                                      //
    Serial.print(F("\nLoop Temp\xC2\xB0\x43 Humid% Press hPa Avg Tmp Avg Hum Avg hPa\n==== ====== ====== ========= ======= ====== =========\n")); // Show header plus unicode "�C" symbol
  } // if-then time to show headers                                                      //
  idx = (idx+1) % NUMBER_READINGS;                                                       // increment and clamp
  BME680.getSensorData(data[idx].temperature, data[idx].humidity,                        // Read once at beginning
                       data[idx].pressure, unused_gas);                                  //
  avg_temperature = avg_humidity = avg_pressure = 0;                                     // Set all values to 0
  for (uint8_t i = 0; i < NUMBER_READINGS; i++)                                          // Compute the sums
  {                                                                                      //
    avg_temperature += data[i].temperature;                                              //
    avg_humidity    += data[i].humidity;                                                 //
    avg_pressure    += data[i].pressure;                                                 //
  } // for-next each measurement                                                         //
  avg_temperature /= NUMBER_READINGS;                                                    // Compute the running
  avg_humidity    /= NUMBER_READINGS;                                                    // averages for readings
  avg_pressure    /= NUMBER_READINGS;                                                    //
  ++loopCounter;                                                                         // increment counter
  if (fast_mode_end != 0 && millis() > fast_mode_end)
  {
    Serial.println("Turning off FAST mode");
    fast_mode_end = 0; // turn off fast mode
    normalMode();
  } // if-then we have gone past the fast mode time
  if (fast_mode_end == 0 && 
    (abs(data[idx].temperature - avg_temperature) > TEMPERATURE_TRIP ||
    abs(data[idx].humidity - avg_humidity) > HUMIDITY_TRIP ||
    abs(data[idx].pressure - avg_pressure) > PRESSURE_TRIP))
  {
    Serial.println("Turning on FAST mode");
    fast_mode_end = millis() + FAST_MODE_DURATION;
    accurateMode();
  } // if-then we change to higher-speed readings

  sprintf(buf, "%4d %3d.%02d", loopCounter, 
          (int8_t)(data[idx].temperature/100),
          (uint8_t)(data[idx].temperature%100));                                  // Temperature in decidegrees
  Serial.print(buf); 
  sprintf(buf, "%3d.%03d", (int8_t)(data[idx].humidity/1000),
          (uint16_t)(data[idx].humidity%1000));                                   // Humidity in milli-percent
  Serial.print(buf);
  sprintf(buf, "%7d.%02d ", (int16_t)(data[idx].pressure/100),
          (uint8_t)(data[idx].pressure%100));                                     // Pressure in Pascals
  Serial.print(buf);
  sprintf(buf, " %3d.%02d", 
    (int8_t)(avg_temperature/100),(uint8_t)(avg_temperature%100));                       // Temperature in decidegrees
  Serial.print(buf);                                                                     //
  sprintf(buf, " %3d.%03d", (int8_t)(avg_humidity/1000),(uint16_t)(avg_humidity%1000));  // Humidity in milli-percent
  Serial.print(buf);                                                                     //
  sprintf(buf, "%6d.%02d\n", (int16_t)(avg_pressure/100),(uint8_t)(avg_pressure%100));   // Pressure in Pascals
  Serial.print(buf);                                                                     //
  /********************************************************
  ** Put the output string together and write to SD-Card **
  ********************************************************/
  sprintf(buf, "%d,%ld,%d.%02d,%3d.%03d,%d.%02d\n", 
    loopCounter, 
    (millis() / 1000), 
    (int8_t)(data[idx].temperature / 100),
    (uint8_t)(data[idx].temperature % 100),
    (int8_t)(data[idx].humidity / 1000),
    (uint16_t)(data[idx].humidity % 1000), 
    (int16_t)(data[idx].pressure / 100),
    (uint8_t)(data[idx].pressure % 100));

  dataFile.print(buf);
  if (idx == 0) dataFile.flush();                                            // force a SD write every cycle
  delay(LONG_DELAY);                                                                // Wait 10s before repeating
} // of method loop()