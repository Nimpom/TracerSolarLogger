/*
 * An interface to the Tracer solar regulator.
 * Communicating in a way similar to the MT-5 display
 */
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <Sleep_n0m1.h>

SoftwareSerial myserial(2, 3); // RX, TX

//Init-string for the Tracer
uint8_t start[] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xEB, 0x90, 0xEB, 0x90, 0xEB, 0x90 };

Sleep sleep;

uint8_t buff[64];

//Defineing Temp Stuff
#define sensor1 A1
#define sensor2 A2
#define DHTTYPE DHT11
DHT dht1(sensor1, DHTTYPE);
DHT dht2(sensor2, DHTTYPE);

//Real Time Clock object
RTC_DS1307 RTC;

//Setup for the LCD

// Define I2C Address where the PCF8574A is
#define I2C_ADDR    0x27

#define BACKLIGHT_PIN  3
#define En_pin  2
#define Rw_pin  1
#define Rs_pin  0
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7

//SD-logging
const byte chipSelect = 10;
byte logged = 0;
File logfile;

//Buttons
const byte resetBUTTONpin = 7;
const byte prevBUTTONpin = 6;
const byte nextBUTTONpin = 9;
const byte loadBUTTONpin = 8;

//IR-sensor
const byte irPin = 4;

//Button variables
byte displayNo = 0;
byte oldDisplayNo = 9;
byte lastResetButtonState = 0;
byte lastPrevButtonState = 0;
byte lastNextButtonState = 0;
byte lastLoadButtonState = 0;

//Power variables
float battery_V = 0;  //Current voltage on the battery
float pv_V = 0;       //Current voltage on the solar panels
float load_A = 0;     //Current power consumption
float loadAlog = 0;   //Used to calculate avarage power consumption during log period
float charge_A = 0;   //Current charge current
float chargeAlog = 0; //Used to calculate avarage charge current during log period
float chargeAs = 0;   //Charged As for the day
float chargeWs = 0;   //Charged Ws for the day
float loadAs = 0;     //Consumed As for the day
float loadWs = 0;     //Consumed Ws for the day
float startVbatt = 0; //Battery start voltage at 00:00. Used to see if battery voltage has gone up or down since last day.
float chargeAmax = 0; //Max charge current
float chargeWmax = 0; //Max charge power
float loadAmax = 0;   //Max current consumtion
float loadWmax = 0;   //Max power consumption
byte loadOnOff = 0;
int numberOfReadings = 0; //Number of readings during log period. Used to calculate avarage.
DateTime powerMaxReset;

//Temperature variables
float temp1 = 0;       //Current temperature inside
float temp2 = 0;       //Current temperature outside
byte h1 = 0;           //Current humidity inside
byte h2 = 0;           //Current humidity outside
float temp1max = 0;    //Max temperature inside
float temp1min = 0;    //Min temperature inside
float temp2max = 0;    //Max temperature outside
float temp2min = 0;    //Min temperature outside
DateTime tempReset;    //Temperature data last reset

//Misc variables
byte lastSecond = 0;

LiquidCrystal_I2C       lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin);
//#####################################SETUP####################################
void setup() {
	//Serial.begin(9600);
        
        //Used to communicate with the Tracer solar regulator
	myserial.begin(9600);
        
        //LCD-panel
        lcd.begin (20,4,LCD_5x8DOTS);
        lcd.setBacklightPin(BACKLIGHT_PIN,POSITIVE); // init the backlight 
        
        //Buttons
        pinMode(resetBUTTONpin, INPUT_PULLUP);
        pinMode(prevBUTTONpin, INPUT_PULLUP);
        pinMode(nextBUTTONpin, INPUT_PULLUP);
        pinMode(loadBUTTONpin, INPUT_PULLUP);
        
        //IR-sensor
        pinMode(irPin, INPUT);
        
        Wire.begin();  
        
        //SD-logging
        pinMode(10, OUTPUT);
        
        //Temperature sensors
        dht1.begin();
        dht2.begin();
        
        powerMaxReset = RTC.now();
        tempReset = RTC.now();
}
//###################################END SETUP##################################
//#####################################LOOP#####################################
void loop() {	
        DateTime now;
        //Reads time
        now = RTC.now();
        //Reads button presses
        readButtons();
        //Retrives data every second
        if(now.second() > lastSecond || (now.second() == 0 && lastSecond > 0)){
            lastSecond = now.second();
            readTracerData();
        }
        
        //Logs to SD-card every 10 min.
        if (now.minute() == 0 || now.minute() == 10 || now.minute() == 20 || now.minute() == 30 || now.minute() == 40 || now.minute() == 50) {
            if(logged == 0){
                //Checks that SD-card is inserted
                checkSDcard();
                //Reads temperatures
                readTemp();
                //Write to SD-card
                doLogging(now);
                logged = 1;
            }
        }else{
            logged = 0;
        }
        
        
        //Check IR-sensor and turns on the display
        if(digitalRead(irPin) == HIGH){
            lcd.setBacklight(HIGH);
            //Show the selected display.
            selectDisplay(now);
            sleep.pwrDownMode(); //set sleep mode
            sleep.sleepDelay(200); //sleep for: sleepTime
        }else{
            lcd.setBacklight(LOW);
            lcd.clear();
            displayNo = 0;
            oldDisplayNo = 9;
            sleep.pwrDownMode(); //set sleep mode
            sleep.sleepDelay(1000); //sleep for: sleepTime
        }
        
        //Reset counters for the day at the time 00:00:00
        if (now.hour() == 0 && now.minute() == 0 && now.second() < 3){
            chargeAs = 0;   //Charged As for the day
            chargeWs = 0;   //Charged Ws for the day
            loadAs = 0;     //Consumed As for the day
            loadWs = 0;     //Consumed Ws for the day
            startVbatt = 0; //Battery start voltage at 00:00
        }
        
        //lcd.setCursor (9,1);
        //lcd.print(freeRam());
}
//###################################End LOOP#####################################
//###################################FUNKTIONS####################################
/*
//Displays amount of free memory
int freeRam () 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
*/
//----------------------------- Read buttons ------------------------------------
void readButtons(){    
      if(digitalRead(resetBUTTONpin) == LOW && lastResetButtonState == HIGH){
        //Reset min max temperature data
        if(displayNo == 5){
          temp1max = 0;
          temp1min = 0;
          temp2max = 0;
          temp2min = 0;
          tempReset = RTC.now();
        }
        //Reset load/charge top records
        if(displayNo == 2){
          chargeAmax = 0;
          chargeWmax = 0;
          loadAmax = 0;
          loadWmax = 0;
          powerMaxReset = RTC.now();
        }
      }
      //Cycle through screens on the display
      if(digitalRead(nextBUTTONpin) == LOW && lastNextButtonState == HIGH){
        displayNo++;
        lcd.clear();
        if(displayNo>4)
          displayNo=0;
      }
      if(digitalRead(prevBUTTONpin) == LOW && lastPrevButtonState == HIGH){
        displayNo--;
        lcd.clear();
        if(displayNo<0)
          displayNo=4;
      }
      //Turn on/off output to load
      if(digitalRead(loadBUTTONpin) == LOW && lastLoadButtonState == HIGH){
          
          uint8_t id = 0x00;
          myserial.write(start, sizeof(start));
  	  myserial.write(id);
          if(loadOnOff){
              //Off command
              uint8_t cmd[] = { 0xAA, 0x01, 0x00, 0x5D, 0xDB, 0x7F };
              myserial.write(cmd, sizeof(cmd));
          }else{
              //On command
              uint8_t cmd[] = { 0xAA, 0x01, 0x01, 0x4D, 0x9A, 0x7F };
              myserial.write(cmd, sizeof(cmd));
          }
          //Read the respons and discard it
          for (int i = 0; i < 255; i++){
              if (myserial.available()) {
                  buff[0] = myserial.read();
              }
          }
      }
    
      lastResetButtonState = digitalRead(resetBUTTONpin);
      lastNextButtonState = digitalRead(nextBUTTONpin);
      lastPrevButtonState = digitalRead(prevBUTTONpin);
      lastLoadButtonState = digitalRead(loadBUTTONpin);
}

//--------------------------- Select Screen to display -----------------------------
void selectDisplay(DateTime now){
      switch(displayNo){
          case 0:
            displayRealtime(now);
            break;
          case 1:
            displayAverage();
            break;
          case 2:
            displayMaxPower();
            break;
          case 3:
            displayTemp();
            break;
          case 4:
            displayTempMinMax();
            break;
      }
      oldDisplayNo = displayNo;
}
//------------------------------- Check SD-card----------------------------------------
void checkSDcard(){
      if (!SD.begin(chipSelect)) { 
          //Serial.println ("No card in slot!");
          lcd.clear(); // clear display, set cursor position to zero
          lcd.setBacklight(HIGH);     // Backlight on
          lcd.setCursor (2,0); 
          lcd.print(F("No card in slot!"));
          while(!SD.begin(chipSelect));
          oldDisplayNo = 9;
          lcd.clear();
      }
}

//--------------------------- Reads data from Charge controler -----------------------------------
void readTracerData(){
        //Serial.println("Reading from Tracer");
        
        uint8_t id = 0x16;
        uint8_t cmd[] = { 0xA0, 0x00, 0xB1, 0xA7, 0x7F };
        

        
	myserial.write(start, sizeof(start));
	myserial.write(id);
	myserial.write(cmd, sizeof(cmd));

	int read = 0;

	for (int i = 0; i < 255; i++){
		if (myserial.available()) {
			buff[read] = myserial.read();
			read++;
		}
	}
        /*
          9 = Battery voltage
         10 = ??
         11 = Panel voltage
         12 = ??
         13-14 reserved
         15 = Current consumption
         16 = ??
         17 = Over discharge
         18 = ??
         19 = Max battery voltage
         20 = ??
         21 = Load On/Off (boolean)
         22 = Overload (boolean)
         23 = Load short (boolean)
         24 = reserved
         25 = Battery overload
         26 = Over discharge (boolean)
         27 = Full battery? (boolean)
         28 = Charging (boolean)
         29 = Battery temp
         30 = Charge current
        */
        
        /*        
        for (int i = 0; i < read; i++){ Serial.print(to_float(buff, i)); Serial.print(" "); }
        Serial.println();
        Serial.println(read);       
        */
        
        battery_V = to_float(buff, 9);    //Battery voltage
        pv_V = to_float(buff, 11);        //Panel voltage
        load_A = to_float(buff, 15);      //Load current
        charge_A = to_float(buff, 30);    //Charge current
        
        loadOnOff = buff[21];
        
        //To calculate avarage during log period
        chargeAlog = chargeAlog + charge_A;
        loadAlog = loadAlog + load_A;
        //Number of readings to calculate avarage
        numberOfReadings++; 
        
        //To calculate the load and charge during the day
        loadAs = loadAs + (load_A);
        loadWs = loadWs + (load_A*battery_V);
        chargeAs = chargeAs + (charge_A);
        chargeWs = chargeWs + (charge_A*battery_V);
        if(startVbatt <= 0)
            startVbatt = battery_V;
        
        //Check max
        if(charge_A > chargeAmax)
          chargeAmax = charge_A;
        if((charge_A * battery_V) > chargeWmax)
          chargeWmax = charge_A * battery_V;
        if(load_A > loadAmax)
          loadAmax = load_A;
        if((load_A * battery_V) > loadWmax)
          loadWmax = load_A * battery_V;
}
//------------------------------- Tracer to float ------------------------------------
float to_float(uint8_t* buffer, int offset){
	unsigned short full = buffer[offset+1] << 8 | buff[offset];

	return full / 100.0;
}
//--------------------------- Reads temperatures -------------------------------------
void readTemp(){
      float h1 = dht1.readHumidity();
      float temp1 = dht1.readTemperature();
      float h2 = dht2.readHumidity();
      float temp2 = dht2.readTemperature();
      
      // Check if any reads failed and exit early (to try again).
      if (isnan(h1) || isnan(temp1) || isnan(h2) || isnan(temp2)) {
          //Serial.println("Failed to read from DHT sensor!");
          return;
      }
}
//----------------------------- Logs to SD-card ---------------------------------------
void doLogging(DateTime nu){
  char filename[]="2014.csv";
  //Sets filname to the year. eg. 2015.csv
  char extension[] = "CSV";
  sprintf(filename,"%04u.%s",nu.year(),extension);
  logfile = SD.open(filename, FILE_WRITE);
  if (logfile) {
    char dateBuffer[10];
    // log time
    sprintf(dateBuffer,"%04u-%02u-%02u",nu.year(),nu.month(),nu.day());
    logfile.print(dateBuffer);
    logfile.print(F(","));
    sprintf(dateBuffer,"%02u:%02u:%02u",nu.hour(),nu.minute(),nu.second());
    logfile.print(dateBuffer);
    logfile.print(F(","));
    logfile.print(battery_V);
    logfile.print(F(","));
    logfile.print((chargeAlog/numberOfReadings));
    logfile.print(F(","));
    logfile.print((loadAlog/numberOfReadings));
    logfile.print(F(","));
    logfile.print(temp1);
    logfile.print(F(","));
    logfile.print(h1);
    logfile.print(F(","));
    logfile.print(temp2);
    logfile.print(F(","));
    logfile.print(h2);
    logfile.println();
    numberOfReadings = 0;
    loadAlog = 0;
    chargeAlog = 0;
  }
  logfile.close();
}
//-------------------- Formats digits for LCD to the format 000.0 ---------------------
void formatDigitsLCD(float digits){
  if(digits <= 999.96){
    if(digits <= 99.96)
      lcd.print(F(" "));
    if(digits < 9.96)
      lcd.print(F(" "));
    lcd.print(digits,1);
  }else{
    if(digits < 9999.96)
      lcd.print(F(" "));
    lcd.print((digits/1000),1);
    lcd.print(F("k"));
  }
}
//###################################END FUNKTIONS##################################
//#####################################DISPLAYS#####################################
//----------------------------- Display real time info ----------------------------------
void displayRealtime(DateTime nu){
  char dateBuffer[10];
  sprintf(dateBuffer,"%02u:%02u:%02u",nu.hour(),nu.minute(),nu.second());
  
  if(oldDisplayNo != displayNo){
      lcd.setCursor (2,0);
      lcd.print(F("PV           LOAD")); 
      lcd.setCursor (5,1);
      lcd.print(F("A             A"));
      lcd.setCursor (5,2);
      lcd.print(F("W  BATT       W"));
  }
  lcd.setCursor (6,0); 
  lcd.print(dateBuffer);
  lcd.setCursor (0,1);
  formatDigitsLCD(charge_A);
  lcd.setCursor (0,2);
  formatDigitsLCD((charge_A*battery_V));
  if(load_A > 0.03){
      lcd.setCursor (14,1);
      formatDigitsLCD(load_A);
      lcd.setCursor (14,2);
      formatDigitsLCD((load_A*battery_V));
  }else{
      lcd.setCursor (14,1);
      formatDigitsLCD(0);
      lcd.setCursor (14,2);
      formatDigitsLCD(0);
  }
  lcd.setCursor (7,3);
  lcd.print(battery_V);
  if(oldDisplayNo != displayNo)
      lcd.print(F("V"));
  
  if(loadOnOff){
      lcd.setCursor(15, 3);
      lcd.print(F("L:On "));
  }else{
      lcd.setCursor(15, 3);
      lcd.print(F("L:Off"));
  }
}
//-------------------------------- Display avarage ------------------------------
void displayAverage(){
  if(oldDisplayNo != displayNo){
      lcd.setCursor (0,0);
      lcd.print(F("CHARGE  IDAG    LOAD")); 
      lcd.setCursor (5,1);
      lcd.print(F("Ah           Ah"));
      lcd.setCursor (5,2);
      lcd.print(F("Wh           Wh"));       
  }
  lcd.setCursor (0,1);
  formatDigitsLCD((chargeAs/3600));
  lcd.setCursor (13,1);
  formatDigitsLCD((loadAs/3600));
  lcd.setCursor (0,2);
  formatDigitsLCD((chargeWs/3600));
  lcd.setCursor (13,2);
  formatDigitsLCD((loadWs/3600));
  if(charge_A <= 0){
    lcd.setCursor (8,2);
    lcd.print(F("BATT"));
    lcd.setCursor (7,3);
    if((battery_V-startVbatt)>=0){
      lcd.print(F("+"));
    }
    lcd.print((battery_V-startVbatt));
    lcd.print(F("V")); 
  }else{
    lcd.setCursor (8,2);
    lcd.print(F("    "));
    lcd.setCursor (7,3);
    lcd.print(F("      "));
  }
}
//--------------------------- Display Max Power ----------------------------
void displayMaxPower(){
  if(oldDisplayNo != displayNo){
      lcd.setCursor (0,0);
      lcd.print(F("CHARGE   MAX    LOAD"));
      lcd.setCursor (5,1);
      lcd.print(F("A             A"));
      lcd.setCursor (5,2);
      lcd.print(F("W             W")); 
      lcd.setCursor(1,3);
      lcd.print (F("Sedan:"));
  }
  lcd.setCursor (0,1);
  formatDigitsLCD(chargeAmax);
  lcd.setCursor (14,1);
  formatDigitsLCD(loadAmax);
  lcd.setCursor (0,2);
  formatDigitsLCD(chargeWmax);
  lcd.setCursor (14,2);
  formatDigitsLCD(loadWmax);
  char dateBuffer[10];
  sprintf(dateBuffer,"%02u%02u%02u",powerMaxReset.year()%100,powerMaxReset.month(),powerMaxReset.day());
  lcd.setCursor (7,3);
  lcd.print(dateBuffer);
  lcd.setCursor (14,3);
  sprintf(dateBuffer,"%02u:%02u",powerMaxReset.hour(),powerMaxReset.minute());
  lcd.print(dateBuffer);
}
//------------------------------- Display temperatures ------------------------------
void displayTemp(){
  if(oldDisplayNo != displayNo){
      lcd.setCursor (2,0);
      lcd.print(F("INNE  TEMP  UTE")); 
  }
  lcd.setCursor (0,1);
  lcd.print(F("T"));
  lcd.setCursor (2,1);
  lcd.print(temp1, 1);
  lcd.write(char(223));
  lcd.print(F("C"));
  lcd.setCursor (12,1);
  lcd.print(F("T"));
  lcd.setCursor (14,1);
  lcd.print(temp2, 1);
  lcd.write(char(223));
  lcd.print(F("C"));
  lcd.setCursor (0,2);
  lcd.print(F("H"));
  lcd.setCursor (2,2);
  lcd.print(h1);
  lcd.print(F("%"));
  lcd.setCursor (12,2);
  lcd.print(F("H"));
  lcd.setCursor (14,2);
  lcd.print(h2);
  lcd.print(F("%"));  
}
//--------------------------- Display Max/Min temperatures ----------------------------
void displayTempMinMax(){
  if(oldDisplayNo != displayNo){
      lcd.setCursor (2,0);
      lcd.print(F("INNE  TEMP  UTE"));
      lcd.setCursor (1,3);
      lcd.print (F("Sedan:"));
  }
  lcd.setCursor (0,1);
  lcd.print(F("M"));
  lcd.setCursor (1,1);
  formatDigitsLCD(temp1max);
  lcd.write(char(223));
  lcd.print(F("C"));
  lcd.setCursor (12,1);
  lcd.print(F("M"));
  lcd.setCursor (13,1);
  formatDigitsLCD(temp2max);
  lcd.write(char(223));
  lcd.print(F("C"));
  lcd.setCursor (0,2);
  lcd.print(F("m"));
  lcd.setCursor (1,2);
  formatDigitsLCD(temp1min);
  lcd.write(char(223));
  lcd.print(F("C"));
  lcd.setCursor (12,2);
  lcd.print(F("m"));
  lcd.setCursor (13,2);
  formatDigitsLCD(temp2min);
  lcd.write(char(223));
  lcd.print(F("C"));
  char dateBuffer[10];
  sprintf(dateBuffer,"%02u%02u%02u",tempReset.year()%100,tempReset.month(),tempReset.day());
  lcd.setCursor (7,3);
  lcd.print(dateBuffer);
  lcd.setCursor (14,3);
  sprintf(dateBuffer,"%02u:%02u",tempReset.hour(),tempReset.minute());
  lcd.print(dateBuffer);
}
//###################################END DISPLAYS#####################################
