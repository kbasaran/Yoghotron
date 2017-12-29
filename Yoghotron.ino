#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address
OneWire  ds(8);  // (a 4.7K resistor is necessary)

#define SWITCHPIN 4
#define FANPIN 6
#define MOSFETPIN 10
#define LEDPIN 13
#define TEMPMIN 30
#define TEMPMAX 50
#define TEMPNOTSAFE 70
#define TWAIT 35

float Tset = 45;
float T1 = 88.8;
float T2 = 88.8;
float Ty = Tset;
short counter;
boolean allSafe = true;
long cycle = 0;
boolean heaterOn = false;
boolean fanOn = true;
float timeLeft = 60 * 14; //in minutes
float timePerCycle = 1.175; //in minutes
short mode = 0; //0 waiting, 1 cooking, 2 cooling, 3 finished, 4 not safe
long lastAction = 0;
short screenSleep = 0;
short dutyCycle = 0;
byte present1 = 0;
byte present2 = 0;
float rampDown = 0.3;
float tempFromRef;

//joystick
short jsx, jsy;
boolean swPressed = false;


//DS18B20
byte addr1[8] = {40, 212, 1, 8, 0, 0, 128, 91};
byte addr2[8] = {40, 255, 149, 174, 81, 22, 4, 102};
byte data[12];
byte i;

void setup()
{
  delay(500);
  Serial.begin(9600);
  Serial.println("Hello!");
  pinMode(SWITCHPIN, INPUT);
  pinMode(MOSFETPIN, OUTPUT);
  pinMode(FANPIN, OUTPUT);
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(FANPIN, HIGH);
  digitalWrite(SWITCHPIN, HIGH);
  lcd.begin(20, 4);              // initialize the lcd
  lcd.home();
  lcd.setCursor ( 3, 1 );
  lcd.print ("The Yoghotron");
  lcd.setCursor ( 6, 3 );
  lcd.print ("S/N:0001");
  delay(5000);
  lcd.clear();
}

void loop()
{
  //Read buttons------------------------------------------------------------------------
  jsx = (analogRead(A2) - 493) / 32;
  jsy = (analogRead(A1) - 493) / 32;
  while (digitalRead(SWITCHPIN) == LOW) swPressed = true;
  if (abs(jsx + jsy + swPressed) > 0 && screenSleep != 0) lastAction = cycle, screenSleep--;


  //Read  sensor 1------------------------------------------------------------------
  if (counter % (200) == 26) {

    if (OneWire::crc8(addr1, 7) != addr1[7]) {
      Serial.println("CRC is not valid!");
      return;
    }
    ds.reset();
    ds.select(addr1);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  }
  if (counter % (200) == 76) {
    present1 = ds.reset();
    ds.select(addr1);
    ds.write(0xBE);         // Read Scratchpad

    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
    }

    // Convert the data to actual temperature
    // because the result is a 16 bit signed integer, it should
    // be stored to an "int16_t" type, which is always 16 bits
    // even when compiled on a 32 bit processor.
    int16_t raw = (data[1] << 8) | data[0];
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time

    T1 = (float)raw / 16.0;
  }


  //Read  sensor 2------------------------------------------------------------------
  if (counter % (200) == 1) {

    if (OneWire::crc8(addr2, 7) != addr2[7]) {
      Serial.println("CRC is not valid!");
      return;
    }
    ds.reset();
    ds.select(addr2);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  }
  if (counter % (200) == 51) {
    present2 = ds.reset();
    ds.select(addr2);
    ds.write(0xBE);         // Read Scratchpad

    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
    }

    // Convert the data to actual temperature
    // because the result is a 16 bit signed integer, it should
    // be stored to an "int16_t" type, which is always 16 bits
    // even when compiled on a 32 bit processor.
    int16_t raw = (data[1] << 8) | data[0];
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time

    T2 = (float)raw / 16.0;
  }


  //All safe?
  if (cycle > 0 && counter > 101 && (Ty > TEMPNOTSAFE || T2 > TEMPNOTSAFE - 25 || present1 != 1 || present2 != 1)) allSafe = false;

  // Are sensors measuring similar?
  lcd.setCursor ( 19 , 0 );
  if (abs(T1 - T2) > 20) lcd.print("!");
  else lcd.print(" ");


  //Calculate duty cycle - temp regulation
  Ty = T1;
  if (mode == 1) tempFromRef = Tset - Ty;
  else tempFromRef = TWAIT - Ty;

  if (mode < 2) {
    dutyCycle = tempFromRef * 140 + 20;
    if (dutyCycle > 99) dutyCycle = 100;
    if (dutyCycle < 1) dutyCycle = 0;
  }
  else dutyCycle = 0;


  //Fan regulation
  if ((mode < 2 && Ty > Tset + 1.5) || mode == 2 || mode == 4 || (mode == 1 && (cycle % 50 == 49))) fanOn = true;
  else fanOn = false;


  //Write heater
  if (dutyCycle > (counter) % 100) heaterOn = true;
  else heaterOn = false;
  if (heaterOn == true & allSafe == true) digitalWrite(MOSFETPIN, HIGH);
  else digitalWrite(MOSFETPIN, LOW);


  //Write fan
  if (counter % 10 == 0) {
    if (fanOn == true) digitalWrite(FANPIN, HIGH);
    else digitalWrite(FANPIN, LOW);


    //LEDPIN Regulation
    if (mode == 1) digitalWrite(LEDPIN, HIGH);
    else digitalWrite(LEDPIN, LOW);

  }


  //Read controls
  if (screenSleep == 0 && mode == 0) {
    timeLeft = timeLeft + jsx;
    Tset = Tset + (float)jsy / 150;
  }
  if (screenSleep == 0 && swPressed == true) {
    if (mode > 1) mode = 0;
    else mode++, mode = mode % 2;
    swPressed = false;
  }
  if (Tset > TEMPMAX) Tset = TEMPMAX;
  if (Tset < TEMPMIN) Tset = TEMPMIN;
  if (timeLeft > 60 * 24) timeLeft = 60 * 24;
  if (timeLeft < 1) timeLeft = 0;


  //Write screen-------------------------------------------------------------------
  //Print status
  short pointTimer = counter % 60;
  lcd.home ();
  if (allSafe == false) {
    mode = 4;
    lcd.print ("   ---Not safe---  ");
    if (pointTimer % 10 < 5) lcd.backlight();
    else lcd.noBacklight();
  }
  else {
    if (mode < 2 && cycle - lastAction > 50) lcd.noBacklight(), screenSleep = 5;
    else lcd.backlight();
    if (mode == 0) lcd.print ("Waiting orders...  ");
    if (mode == 1) {
      lcd.print ("Processing");
      lcd.setCursor ( 10, 0 );
      if (pointTimer < 15)                    lcd.print ("         ");
      if (pointTimer > 14 && pointTimer < 30) lcd.print (".        ");
      if (pointTimer > 29 && pointTimer < 45) lcd.print ("..       ");
      if (pointTimer > 44 && pointTimer < 60) lcd.print ("...      ");
    }
    if (mode == 2) {
      lcd.print ("Cooling");
      lcd.setCursor ( 7, 0 );
      if (pointTimer < 15)                    lcd.print ("            ");
      if (pointTimer > 14 && pointTimer < 30) lcd.print (".           ");
      if (pointTimer > 29 && pointTimer < 45) lcd.print ("..          ");
      if (pointTimer > 44 && pointTimer < 60) lcd.print ("...         ");
    }
    if (mode == 3) {
      lcd.print ("Yoghurt ready!     ");
      if (pointTimer % 30 < 15) lcd.backlight();
      else lcd.noBacklight();
    }
  }

  //Print time left
  lcd.setCursor ( 0, 1 );
  if (mode == 0) lcd.print("Time set:"), lcd.setCursor ( 9, 1 );
  else lcd.print("Time left:"), lcd.setCursor ( 10, 1 );
  if (timeLeft < 60 * 10) lcd.print(" 0" + String(int(timeLeft) / 60) + "h");
  else lcd.print(" " + String(int(timeLeft) / 60) + "h");
  if (mode == 0) lcd.setCursor ( 13, 1 );
  else lcd.setCursor ( 14, 1 );
  if (int(timeLeft) % 60 < 10) lcd.print("0" + String(int(timeLeft) % 60) + "m ");
  else lcd.print(String(int(timeLeft) % 60) + "m");

  //Print state variables
  lcd.setCursor ( 0, 2 );
  lcd.print(String(Tset, 1) + "," + String(Ty, 1) + "," + String(T2, 1));

  //Print state variables 2
  lcd.setCursor ( 0, 3 );
  lcd.print(String(dutyCycle) + "%  ");
  lcd.setCursor ( 6, 3 );
  lcd.print(String(digitalRead(MOSFETPIN)) + "," + String(digitalRead(FANPIN)) + "," + String(cycle) + "," + String(rampDown, 1) + "   ");

  // Increment counter and cycle
  if (counter % 1000 == 999 && mode > 0 && mode < 3 ) {
    Serial.print("Cycle/Temp/Duty: " + String(Ty, 1));
    Serial.println(" / " + String(cycle) + " / " + String(dutyCycle));
    cycle++;
    // Do the temp ramp down
    Tset = Tset - timePerCycle / 60 * rampDown;
    if (Tset < TEMPMIN) Tset = TEMPMIN;

    //Countdown timer and mode cooling and finishing
    if (timeLeft > timePerCycle * 1.5) timeLeft = timeLeft - timePerCycle;
    else {
      timeLeft = 0;
      if (Ty > TEMPMIN) mode = 2;
      else mode = 3;
    }
  }
  if (mode == 0) cycle = 0;
  counter = (counter + 1) % 10000;
}

