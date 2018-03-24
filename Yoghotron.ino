#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>

#define TEMPPIN 11
#define SWITCHPIN 4
#define FANPIN 6
#define MOSFETPIN 10
#define LEDPIN 13
#define TEMPMINSETTING 30
#define TEMPMAXSETTING 50
#define T2NOTSAFE 45
#define T2STOPHEATING 40
#define TWAITBOX 33

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address
OneWire  ds(TEMPPIN);  // (a 4.7K resistor is necessary)

float timeLeft = 60 * 12; //in minutes
float timePerCycle = 1.265774; //in minutes
float Tset = 45;
float rampDown = 0.3;

float T1 = 88.8;
float T2 = 88.8;
float Ty = Tset;
short counter;
boolean allSafe = true;
long cycle = 0;
boolean heaterOn = false;
boolean fanOn = true;
short mode = 0; //0 waiting, 1 cooking, 2 cooling, 3 finished, 4 not safe
long lastAction = 0;
short screenSleep = 0;
short dutyCycle = 0;
byte present1 = 0;
byte present2 = 0;
float tempError;
float cumErr = 0;

//joystick
short jsx, jsy;
boolean swPressed = false;


//DS18B20
byte addr1[8] = {40, 255, 131, 148, 112, 23, 3, 173}; //ROM = 28 D4 1 8 0 0 80 5B
byte addr2[8] = {40, 255, 173, 103, 70, 22, 3, 197}; //ROM = 28 FF AD 67 46 16 3 C5
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
  if (counter % (100) == 51) {

    if (OneWire::crc8(addr1, 7) != addr1[7]) {
      Serial.println("CRC is not valid!");
      return;
    }
    ds.reset();
    ds.select(addr1);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  }
  if (counter % (100) == 76) {
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
  if (counter % (100) == 1) {

    if (OneWire::crc8(addr2, 7) != addr2[7]) {
      Serial.println("CRC is not valid!");
      return;
    }
    ds.reset();
    ds.select(addr2);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  }
  if (counter % (100) == 26) {
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
  if (counter > 101 && (T2 > T2NOTSAFE || T1 < 4 || T2 < 4 || present1 != 1 || present2 != 1)) allSafe = false;

  //Calculate duty cycle - temp regulation
  Ty = T1;
  if (mode == 1) tempError = Ty - Tset;
  else tempError = T2 - TWAITBOX;

  if (mode < 2) {
    dutyCycle = 20 - tempError * 200;
    if (dutyCycle > 99) dutyCycle = 100;
    if (dutyCycle < 1) dutyCycle = 0;
  }
  else dutyCycle = 0;


  //Fan regulation
  if ((mode == 1 && Ty > Tset + 1.5 && cycle % 3 == 2) || (mode == 1 && Ty > Tset + 3) || mode == 2 || mode == 4 || (mode == 1 && (cycle % 50 == 49))) fanOn = true;
  else fanOn = false;


  //Write heater
  if ((dutyCycle > (counter) % 100) && T2 < T2STOPHEATING) heaterOn = true;
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
  if (Tset > TEMPMAXSETTING) Tset = TEMPMAXSETTING;
  if (Tset < TEMPMINSETTING) Tset = TEMPMINSETTING;
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
    if (mode == 0) {
      lcd.print ("Waiting orders...   ");
      lcd.setCursor (15, 3);
      lcd.print("     ");
    }
    if (mode == 1) {
      lcd.print ("Processing");
      lcd.setCursor ( 10, 0 );
      if (pointTimer < 15)                    lcd.print ("      ");
      if (pointTimer > 14 && pointTimer < 30) lcd.print (".     ");
      if (pointTimer > 29 && pointTimer < 45) lcd.print ("..    ");
      if (pointTimer > 44 && pointTimer < 60) lcd.print ("...   ");
    }
    if (mode == 2) {
      lcd.print ("Cooling");
      lcd.setCursor ( 7, 0 );
      if (pointTimer < 15)                    lcd.print ("         ");
      if (pointTimer > 14 && pointTimer < 30) lcd.print (".        ");
      if (pointTimer > 29 && pointTimer < 45) lcd.print ("..       ");
      if (pointTimer > 44 && pointTimer < 60) lcd.print ("...      ");
    }
    if (mode == 3) {
      lcd.print ("Yoghurt ready!  ");
      if (pointTimer % 30 < 15) lcd.backlight();
      else lcd.noBacklight();
    }
    if (mode > 0) {
      lcd.setCursor (16 , 0);
      if (cumErr < 99.95) lcd.print(String(cumErr, 1));
      else lcd.print(String(cumErr, 0));
    }
  }

  // Is it getting too warm?
  lcd.setCursor ( 19 , 2 );
  if (T2 > T2STOPHEATING) lcd.print("!");
  else lcd.print(" ");

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
  lcd.print(String(digitalRead(MOSFETPIN)) + "," + String(digitalRead(FANPIN)) + "," + String(rampDown, 1) + "," + String(cycle));

  // Increment counter and cycle
  if (counter % 1000 == 999) {
    Serial.print("Cycle/Temp/Duty: " + String(Ty, 1));
    Serial.println(" / " + String(cycle) + " / " + String(dutyCycle));
    if (mode == 1) {
      cycle++;
      // Do the temp ramp down
      Tset = Tset - timePerCycle / 60 * rampDown;
      if (Tset < TEMPMINSETTING) Tset = TEMPMINSETTING;

      //Increment total error
      cumErr = cumErr + abs(tempError) * timePerCycle / (float)60;

      //Decrement time
      timeLeft = timeLeft - timePerCycle;
    }
    //Cooling and finishing
    if (timeLeft < timePerCycle * 1.5 && mode == 1) {
      timeLeft = 0;
      mode = 2;
    }
    if (Ty < TEMPMINSETTING && mode == 2) mode = 3;
  }
  if (mode == 0) {
    cycle = 0;
    cumErr = 0;
  }
  counter = (counter + 1) % 10000;
}

