//#include <Adafruit_NeoPixel.h>
#define BLYNK_FIRMWARE_VERSION        "0.1.0"
#define BLYNK_PRINT Serial
//#define BLYNK_DEBUG
#define APP_DEBUG
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <SPI.h>
#include <SD.h>
#define USE_WEMOS_D1_MINI
#include "BlynkEdgent.h"
FASTLED_USING_NAMESPACE

#define DATA_PIN            2
#define NUM_LEDS            35
#define MAX_POWER_MILLIAMPS 2200
#define LED_TYPE            WS2812B
#define COLOR_ORDER         GRB
CRGB leds[NUM_LEDS];

/////// świecenie tryby

///baza modów świecenia
enum Mode { RAINBOW, PACIFICA, CYLON, LAVA, TWINKLE, PULSE, FIRE, COLORWAVES, SPEED, FULL_TILT };
Mode currentMode = PACIFICA;  //domyślny bo go lubię i chuj
Mode overrideMode = PACIFICA;
bool overrideActive = false;
 

int T_sygnalizacji=10000; ///czas bazowy sygnalizacji
int DELAYVAL=750; //czas bazowy wyświetlania efektów systemowych [ms]
int T_zmiany = 20000; ///czas bazowy przejść między trybami zwykłymi

unsigned long lastSignalTime = 0;
unsigned long lastEffectChange = 0;
unsigned long blinkStartTime = 0;
bool isBlinking = false;

unsigned long gyroEffectStart = 0;
bool gyroEffectActive = false;

unsigned long discoStartTime = 0;
bool discoActive = false;

///do konkretnych trybów świecenia
int cylonPos = 0;
int cylonDir = 1;
uint8_t rainbowHue = 0;
uint16_t pacificaWave = 0;
uint8_t pulseBrightness = 0;
bool pulseUp = true;

float BRIGHTNESS_MAX=250*0.5; //peak jasność

/////INICJALIZACJA MOFUŁÓW
Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

//////do obsługi żyroskopu
float GyroX = 0; /// kąt nachylenia osi X
float GyroY = 0; /// kąt nachylenia osi Y
float GyroZ = 0; /// kąt nachylenia osi Z
float AccXold=0;
float AccYold=0;
float AccZold=9.8;

float Gyro_mix_=0; //
float Acc_mix_=0;
float Gyro_mix_OLD=0, Acc_mix_OLD=0;
float AccX=0;
float AccY=0;
float AccZ=0;
float Acc_treshhold=0.5; // wartość przyspieszenia triggerująca efekty
float Acc_T=800; //czas działania efektów od przyspieszeń
float Gyro_treshhold=25; //no idea, jeśli któraś z osi przekroczy ten próg kubek zacznie świecić póki kąty się nie wyzerują
float pitchFromAccel=0;
float rollFromAccel=0;
double _pitch=0;
double _roll=0;
float _pitch0=0;
float _roll0=0;
double pitch, roll;
float Acc_mix, Gyro_mix;
float GyroEffectDuration=20*1000; //czas trwania efektu od kątu pochylenia
float odchylZgraniczny=8; ///odchył od pionu do uwzględniania przechyłu w przyspieszeniach


///////////ogólne i sprzętowe
#define laduj 0 //enable ładowania ogniw
#define wlancz A0 // pozycja włącznika przelicznik 2,09 (4V->1,9V)
volatile bool changeState = false; //do przerwania obsługującego wyłączenie ładowania
float przelancznik=4; ///napiecie miedzy wlacznikiem i stepUP'em
int pstryczek=0; // stan włącznika; 0==wyłączony; 1= włączone
int ladowanie=1; // 0 == nie ładuj; 1== żądanie ładowania
float termika=0; // czy jest bezpiecznie temicznie, 0==tak, >1 za ciepło
float temp1=20; //temp z ntc
// int Ro = 10, B = 3950; //Nominal resistance 10K, Beta constant
// float Rseries = 9.2;// R Series resistor 10K
// float To = 298.15; // Nominal Temperature
// float Vin=4.92; //napięcie "5V"

////////////ina219
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;

//////////////stan baterii
float SOC; // poziom naładowania %; 2,5V==0; 4,2V==100% 
double mAh; // poziom naładowania w mAh
float mAh1=0, mAh2=0; //pomocnicze
unsigned long previousMillis = 0;
unsigned long czas=0; //czas między petlami
unsigned long currentMillis=0;
long czas_ladowania=0;
long czas_zasilZEW=0;
int naladowane=0;

// D5, D6, D7, D8 // SCK, MISO, MOSI, CS
#define SD_CS_PIN D8

File dataFile; 
int obrot=0; //ilość pętli do zapisu mAh 
 
////////31 wartości do identyfikowania *początkowego* SOC
float napiecia[]={4.2, 4.06, 4.03, 3.99, 3.97, 3.94, 3.93, 3.91, 3.90, 3.89, 3.88, 3.8799, 3.8796, 3.873, 3.871, 3.870, 3.868, 3.867, 3.866, 3.865, 3.864, 3.862, 3.8638, 3.79, 3.43, 3.38, 3.14, 3.03, 2.937, 2.7, 2.4}; //progi napięc w V
float mAh_OCV[]={0,0.001,21.54,78.01, 152.30,229.56,321.693,452.45,586.18,722.88,868.49,1026.02,1141.91,1284.54,1415.36,1543.09,1685.73,1795.69,1899.782,2015.67,2122.58,2214.71,2312.77,2384.11,2443.53,2485.14,2523.74,2547.54,2556.463,2571.322,2575}; //progi pojemności mAh
int mAh_max=2575; //max SOC - zmierzyć kiedyś

///////////////////////////wifi
#define BLYNK_TEMPLATE_ID "TMPL4sdrCTR6X"
#define BLYNK_TEMPLATE_NAME "Kubek V3"
// #define BLYNK_AUTH_TOKEN "g6ZqCuMmhLpPeeWtHkjCvmrmtPy5R79O"


BlynkTimer timer;
BLYNK_WRITE(V1) {
  int val = param.asInt();
  if (val >= 0 && val <= 7)/// to ilość trybów świecenia?
  {
    overrideMode = static_cast<Mode>(val);
    overrideActive = true;
    lastEffectChange = millis();
  } else {
    overrideActive = false;
  }
}
void setup() {

pinMode(laduj, OUTPUT); ///okazuję się że w esp trzeba ustawić tryb pinu... ciekawe ile rzeczy mi przez to nie działało
attachInterrupt(digitalPinToInterrupt(wlancz), handleInterrupt, CHANGE);

Serial.begin(9600);

// pixels.begin(); //start LED
delay(2000); // 3 second delay for boot recovery, and a moment of silence
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection( TypicalLEDStrip );
  FastLED.setMaxPowerInVoltsAndMilliamps( 5, MAX_POWER_MILLIAMPS);
FastLED.clear();
FastLED.show();



////////ina219 miernik prądu
ina219.begin();
delay(500);

/////karta SD i początkowe SOC na bazie zapisu z SD

 if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card initialization failed!");
        while (1);
    }
           // Odczyt lub inicjalizacja pliku "pojemnosc"
    if (SD.exists("pojemnosc"))
    {
        dataFile = SD.open("pojemnosc", FILE_READ);
        if (dataFile) 
        {
            String lastValue;
            while (dataFile.available()) {
                lastValue = dataFile.readStringUntil('\n');
            }
            mAh1 = lastValue.toFloat();  ///odczytaj ostanią wartość pojemności i przypisz ją jako aktualną
            dataFile.close();
            Serial.print("odczytane mAh ");
            Serial.println(mAh1);
        }
    } else 
    {
        dataFile = SD.open("pojemnosc", FILE_WRITE);
        if (dataFile) {
            dataFile.println(mAh);  ///jeśli pliku brak, nadpisz go wartością max (przy definicji)
            dataFile.close();
        }
    }  

//////////////////początkowy SOC z napięcia
busvoltage = ina219.getBusVoltage_V(); 
Serial.print("odczytane napiecie pierwotnie ");
Serial.println(busvoltage);

float mAh2 = findClosestmAh(busvoltage, napiecia, mAh_OCV, 30);

mAh1=mAh1*0.99;  ///odejmujemy za postój, kiedyś do poprawy na bazie modelu baterii
mAh=((1*mAh1)+mAh2)/2; ///szacowanie SOC dla biednych
SOC=mAh_max/mAh*100 ; ///SOC pierwotne

Serial.print("obliczone mAh ");
Serial.println(mAh);


Wire.begin();   ///nie wiem czy to musi tu być
/////////żyroskop konfig
mpu.begin();


   mpu.setAccelerometerRange(MPU6050_RANGE_4_G);  //arbitralnie

  mpu.setGyroRange(MPU6050_RANGE_500_DEG);   //arbitralnie

  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);   //chyba częściej nie ma sensu

////wifi

  BlynkEdgent.begin();
  timer.setInterval(1000L, sendBatteryData);

////  
previousMillis = millis();
}
///////////////////////////////////////////

///////////znajduje napięcie mAh po pierwotnym napięciu z INA219
float findClosestmAh(float busvoltage, float napiecia[], float mAh_OCV[], int size) {
    float closest_mAh = mAh_OCV[0]-100;
    float min_diff = fabs(busvoltage - (napiecia[0]*1000));
    
    for (int i = 1; i < size; i++) {
        float diff = fabs(busvoltage - (napiecia[i]*1000));
        if (diff < min_diff) {
            min_diff = diff;
            closest_mAh = mAh_OCV[i];
        }
    }
    
    return closest_mAh;
}

ICACHE_RAM_ATTR void handleInterrupt() {
    changeState = true;
}

void loop() {
////wifi
BlynkEdgent.run();
timer.run();

///czas
unsigned long now = millis();
czas = now - previousMillis; // 
previousMillis = now;


/////////INA219
pomiary_ele();
obl_SOC();

//////////ładowanie
// temp1= Read_NTC10k(); //temp z NTC
//Serial.println((String)"Temperature in celsius    :" + temp1 + "°C");
//Serial.println(" ");

//kolejność wykonywania ważna, najpierw żądania dbające os SOH ogniw, bezpieczeństwo termiczne tuż przed ładowaniem
tryb_muzeum(); //odpowiada za nie ładowanie permanente przy dłuższym postoju na zasilaniu zew.
term_bezp();
tryb_ladowania(); // odpowiada za ładowanie

///////żyroskop
gyro();

/////wybierz tryb świecenia na podstawie akcji

if(mAh<0.1*mAh_max && now-lastSignalTime >= T_sygnalizacji)
{
  SOC10();
  lastSignalTime = now;
}
else if(mAh<0.05*mAh_max && now-lastSignalTime >= T_sygnalizacji)
{
  SOC5();
  lastSignalTime = now;
}
else if(mAh>0.99*mAh_max && now-lastSignalTime >= T_sygnalizacji && naladowane==1)
{
  SOC100();
  lastSignalTime = now;
}

 // Normalne efekty
else if(!gyroEffectActive && !discoActive)
{
    if (now - lastEffectChange >= T_zmiany) {
      currentMode = static_cast<Mode>((currentMode + 1) % 8); // 8 domyślnych trybów na 05.04.2025
      lastEffectChange = now;
    }
  }

  // Wyzwolenie trybu dyskoteki
if (!discoActive && Acc_mix > Acc_treshhold) {
    discoActive = true;
    discoStartTime = now;
    currentMode = SPEED;
    Serial.print("Disco");
    Serial.println("");
  }

  // Wyzwolenie trybu koloru z żyroskopu
if (!gyroEffectActive && Gyro_mix > Gyro_treshhold) {
    gyroEffectActive = true;
    gyroEffectStart = now;
    currentMode = FULL_TILT;
Serial.print("FullTilt ");
Serial.println("");
  }
  // Wyłączenie efektów specjalnych po czasie
if (discoActive && now - discoStartTime >= Acc_T) {
    discoActive = false;
    currentMode = PACIFICA;
    lastEffectChange = now;
  }

if (gyroEffectActive && now - gyroEffectStart >= GyroEffectDuration) {
    gyroEffectActive = false;
    currentMode = PACIFICA;
    lastEffectChange = now;
  }
switch (currentMode)
{
    
    case PACIFICA:
      EVERY_N_MILLISECONDS(40) { PACIFICA_loop(); }
      break;
    case RAINBOW:
      EVERY_N_MILLISECONDS(20) { rainbowEffect(); }
      break;
    case CYLON:
      EVERY_N_MILLISECONDS(30) { cylonEffect(); }
      break;
       case LAVA:
      EVERY_N_MILLISECONDS(50) { lavaLamp(); }
      break;
    case TWINKLE:
      EVERY_N_MILLISECONDS(100) { ttwinkleFOX(); }
      break;
    case PULSE:
      EVERY_N_MILLISECONDS(30) { pulseFade(); }
      break;
    case FIRE:
      EVERY_N_MILLISECONDS(50) { fireEffect(); }
      break;
    case COLORWAVES:
      EVERY_N_MILLISECONDS(30) { colorWaves(); }
      break;
    case SPEED:
      EVERY_N_MILLISECONDS(50) {  DISCO();   }
      break;
    case FULL_TILT:
      EVERY_N_MILLISECONDS(110) {FullTilt();}
      break;
    
}
Serial.println("");
Serial.println("************");
Serial.print("kąt ");
Serial.println(Gyro_mix);
Serial.print("zmiana przyspieszeń ");
Serial.println(Acc_mix);
Serial.print("tryb świecenia ");
Serial.println(currentMode);
Serial.print("Gyro  ");
Serial.println(gyroEffectActive);
Serial.println("****");
}
///////////////////////////////////////////////////////////////////////////////////////////////
void sendBatteryData() {
  Blynk.virtualWrite(V5, SOC);
  Blynk.virtualWrite(V6, temp1);
}
////////////////////////////////
void tryb_ladowania() ///laduj==1 żądanie ładowania; ładuj == 0 nie ładujemy
{
przelancznik=analogRead(wlancz)*3.3/1024*2.1; ///odczyt napiecia //*2.1<- dzielnik//1024==3,3V
if (przelancznik>-1 && przelancznik<1)
{
  pstryczek=0;
// if (mAh<mAh_max)
// {ladowanie=1;}
}
else
{pstryczek=1;
digitalWrite(laduj,LOW);
}

// Serial.print("napiecie odczytane na przelanczniku ");
// Serial.println(przelancznik);


if(termika==0) ///jak termicznie bezpiecznie, dopiero myśl o ładowaniu
{
  if (pstryczek == 0)   ///jak wyłącznik jest w pozycji off, to dopiero wolno ładować (inaczej jebie zwarciem) 
  {
    if (ladowanie == 1)  //żądanie ładowania =1; nie ładuj=0
    {
      digitalWrite(laduj,HIGH);
    }
    else
    {
    digitalWrite(laduj,LOW);
    }
  }
  else
  {
  digitalWrite(laduj,LOW);
  // ladowanie=0;

Serial.print("flaga ładowania ");
Serial.println(ladowanie);
  }
 
// Serial.println("");
Serial.print("temperatura ");
Serial.println(temp1);
Serial.println(""); 
}
// Serial.print("napiecie odczytane na przelanczniku ");
// Serial.println(przelancznik);
// Serial.print("wlancznik ");
// Serial.println(pstryczek);

}
////////////////////////////
void tryb_muzeum()  /// jak stoisz na wystawie z muzeum to trzymaj tylko SOC w ryzach 40-80%, póki ktoś nie zarząda inaczej
{
  if (czas_zasilZEW>12*3600*1000) ///nie ładuj non stop
{
  naladowane=0; ///zdejmij flage ładowania
  if (mAh<mAh_max*0.4)
  {
  ladowanie=1;   //flaga ładowania, można znowu ładować
  czas_ladowania=0; //zeruj zegar ładowania
  }
  ////////
  if(mAh>mAh_max*0.8) //wyłącz ładowanie jeśli masz powyzej 80%
  {
  ladowanie=0;   //trzymaj flagę ładowania w dole, do czasu aż SOC spadnie poniżej 40%
  }
}

if(czas_zasilZEW>72*3600*1000)  //ew przeładowanie float
{czas_zasilZEW=13*3600*1000;}

}
//////////////////////
void pomiary_ele() ////pomiary elektryczne INA219
{
  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();     
  current_mA = ina219.getCurrent_mA();  // prąd ładowania / rozładowywania (przy zerowym poborze wskazuje między -0,4 a -0,6mA)
  power_mW = ina219.getPower_mW();  //moc do liczenia Wh
  loadvoltage = busvoltage + (shuntvoltage / 1000); //napięcie pakietu 
}
///////////////////////////
void obl_SOC()  ///aktualne SOC baterii 
{
// Serial.print("pojemność przed ");
// Serial.println(mAh);
mAh=mAh+(current_mA*czas/3600000.0); //pojemność == poprzednia pojemność - prąd*czas w h (AKA Culomb counting)   /// upewnić się że prąd w + i * występuje i to w dobre strony
Serial.print("pojemność po ");
Serial.println(mAh);
// Serial.print("czas ");
// Serial.println(czas/3600000.0);

///blokada dziwnych akcji z SOC, tak wiem jebać dokładność
if(mAh>mAh_max)
{
  mAh=mAh_max;
}
if(mAh<0)
{
  mAh=0;
}
///ładowanie i zerowanie SOC dla 3h pod ładowarką
if(current_mA<100) ///upewnić się że prąd w - oznacza ładowanie
{
 
naladowane=1;
  czas_ladowania=czas_ladowania+czas; //zliczaj czas ładowania w ms
  czas_zasilZEW=czas_zasilZEW+czas; //zliczaj czas na zasilaniu zew
  if(czas_ladowania>3*3600*1000) //czas ładowania powyżej 3h zeruje SOC
  { 
    mAh=mAh_max; // "zeruj" SOC
  }
}
else {naladowane=0;} ///flaga naładowania

SOC=2575/mAh*100; // w %

if (obrot >5)
{
dataFile = SD.open("pojemnosc", FILE_WRITE);
        if (dataFile)
        {
            dataFile.println(mAh); //zrzut danych do pliku
            dataFile.close();
        }
obrot=0;   //zeruj licznik pętli     
}
obrot=obrot+1;
manageSDOverflow();

}
/////////////////////////////////////////////////
void gyro()///// gyroskop eventy
{ 
// lastAngleX=GyroX;
//   lastAngleY=GyroY;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

///przyspieszenia w m/s2
 AccX=a.acceleration.x;
 AccY=a.acceleration.y;
 AccZ=a.acceleration.z;      ///9,81 domyślne!
 
///prędkość kątowa rad/s
GyroX=g.gyro.x*czas/1000; // Konwersja deg/s na stopnie bez sekund
GyroY=g.gyro.y*czas/1000; // Konwersja deg/s na stopnie
// GyroZ=g.gyro.z*czas/1000; // Konwersja deg/s na stopnie


////filtorowanie
pitchFromAccel = (atan(AccY / (sqrt(pow(AccX,2) + pow(AccZ, 2)))) * 180 / PI) ; // kąt obrotu w osi Y
rollFromAccel = (atan(AccX / (sqrt(pow(AccY,2) + pow(AccZ, 2)))) * 180 / PI); // kąt obrotu w osi X

float filtrownik=0.6; //pitch/roll GyroFavoring

_pitch = filtrownik * (pitch + GyroY) + (1.00 - filtrownik) * (pitchFromAccel); //obrót w osi Y oficjalnie
_roll = filtrownik * (roll + GyroX) + (1.00 - filtrownik) * (rollFromAccel); //obrót w osi X oficjalnie
pitch=_pitch;
roll=_roll;
////oficjalne wartości
// AccX=abs(AccX);
// AccY=abs(AccY);
// AccZ=abs(AccZ);
// GyroZ=abs(GyroZ);

Gyro_mix=max(abs(roll),abs(pitch)); ///daj max wartość nachyleń i nią wyzwalaj efekty
Acc_mix=max(abs(AccX-AccXold), max(abs(AccY-AccYold), abs(AccZ-AccZold))); //daj max wartość z przyspieszeń i nią wyzwalaj efekty

// if (abs(Acc_mix_-Acc_mix_OLD)>Acc_treshhold)
// {
// Acc_mix=abs(Acc_mix_-Acc_mix_OLD);
// }
// else
// {
// Acc_mix=0;
// }
// Acc_mix_OLD=Acc_mix;

// if(Gyro_mix_ > Gyro_treshhold)
// {
// Gyro_mix=abs(Gyro_mix_-Gyro_mix_OLD);
// }
// else
// {
// Gyro_mix=0;
// }
// Gyro_mix_OLD=Gyro_mix;

AccXold=AccX;
AccYold=AccY;
AccZold=AccZ;

// Serial.print("Gyro mix ");
// Serial.println(Gyro_mix);
// Serial.print("Acc mix ");
// Serial.println(Acc_mix);


// Serial.print("przysp Z ");
// Serial.println(AccZ);
// Serial.print("przysp X ");
// Serial.println(AccX);
// Serial.print("przysp Y ");
// Serial.println(AccY);
// Serial.print("obrot w osi X ");
// Serial.println(_roll);
// Serial.print("obrot w osi Y ");
// Serial.println(_pitch);
// Serial.print("X GYRO ");
// Serial.println(GyroX);
// Serial.print("Y GYRO ");
// Serial.println(GyroY);

///temperatura w *C
temp1= temp.temperature; //wyjebałem NTC, teraz to robi za bezpiecznik termiczny
 

}

void term_bezp()  //// abortuj ładowanie jak jest za gorąco (wodorki nie lubią się przegrzewać)
{
  if (temp1>40)
  {digitalWrite(laduj, LOW);
  ladowanie=0; /// wyłącz żądanie ładowania
  termika=termika+1000;}  //odpocznij od ładowania 1000 pętli 
  else
  {termika=termika-1;} //jak termicznie jest git to dopiero zjedzie do 0

  if (termika<0) //żeby nie wyjść za float + 0 oznacza że jest termicznie bezpiecznie
  {termika=0;}

  if (termika>100000000) //żeby nie wyjść za float z tą karą
  {termika=100;}
}

// float Read_NTC10k() /// czujnik temperatury -> dokładność kartofla
// {
// float Vi = analogRead(A0) * (Vin / 1023.0);
//   //Convert voltage measured to resistance value
//   //All Resistance are in kilo ohms.
//   float R = ((Vin-Vi)*Rseries) / Vi;
//   /*Use R value in steinhart and hart equation
//     Calculate temperature value in kelvin*/
//   float T =  1 / ((1 / To) + ((log(R / Ro)) / B));
//   float tempC = T - 273.15-5; // Converting kelvin to celsius i dla tego egzemplarza -5st xD
//   return tempC;
// }


/////////////////////
void manageSDOverflow() { //obsługa SD żeby jej nie zatkać
    int lineCount = 0;
    dataFile = SD.open("pojemnosc", FILE_READ);
    if (dataFile) {
        while (dataFile.available()) {
            dataFile.readStringUntil('\n');
            lineCount++;
        }
        dataFile.close();
    }

    if (lineCount > 1000) { // Ustaw limit linii
        //Serial.println("SD full, clearing file...");
        SD.remove("pojemnosc");
        dataFile = SD.open("pojemnosc", FILE_WRITE);
        if (dataFile) {
            dataFile.println(mAh);
            dataFile.close();
        }
    }
}

//////////////////////////LEDy
// ///////////////////////////////
///////////////////////////////////////////////warunkowane SOC
void SOC10()  // jak SOC spadnie poniżej 10%
{
        //FastLED.clear(); //zgaś wszystko
        //FastLED.show();
  fill_solid(leds, NUM_LEDS, CRGB(76, 0, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  ///zgaś po ustalonym czasie
  if (isBlinking && millis() - blinkStartTime >= DELAYVAL) {
    isBlinking = false;
  }
}

void SOC5() // jak SOC spadnie poniżej 5%
{
        // FastLED.clear(); //zgaś wszystko
        // FastLED.show();
   for(int i=0; i<NUM_LEDS-1; i=i+5) //co 5 zapal na czerwono
  {
    leds[i] = CRGB(150, 0, 0); ///napierdalaj czerowym
  }
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  if (isBlinking && millis() - blinkStartTime >= DELAYVAL*2) {
    isBlinking = false;
  }
}
void SOC100()  // jak SOC powyżej 99% podczas ładowania
{
        // fill_solid(leds, NUM_LEDS, CRGB(0, 85, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
        // FastLED.clear(); //zgaś wszystko
        // FastLED.show();
        // delay(DELAYVAL/2);
  fill_solid(leds, NUM_LEDS, CRGB(0, 85, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  if (isBlinking && millis() - blinkStartTime >= DELAYVAL/2) {
    isBlinking = false;
  }
}

void DISCO() ///świeci na Acc_T w zależności od przyspieszeń
{
///go random
  for (int i = 0; i < NUM_LEDS; i++)
  { 
    int posX = random(NUM_LEDS);
    leds[posX] = CHSV(94,255, random(0, 200)); ///zielony X
    leds[posX] = CRGB::Black; // Zgaś tę diodę przy następnym wywołaniu
        
    int posZ = random(NUM_LEDS);
    leds[posZ] = CHSV(2, 255,random(0, 200)); //czerwony Z
    leds[posZ] = CRGB::Black; // Zgaś tę diodę przy następnym wywołaniu

    int posY = random(NUM_LEDS);
    leds[posY] = CHSV(157, 255,random(0, 200)); //niebieski Y
    leds[posY] = CRGB::Black; // Zgaś tę diodę przy następnym wywołaniu
  }
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  if (isBlinking && millis() - blinkStartTime >= Acc_T) {
    isBlinking = false;
  }

}
void FullTilt() //świeci w zależności od przechyłu (500* niby max)
{
   // Normalizacja koloru do zakresu 0–255
  // uint8_t red   = map(GyroZ, 0, 500, 0, 255);
  uint8_t green = map(pitch, 0, 100, 0, 255);
  uint8_t blue  = map(roll, 0, 100, 0, 255);
  uint8_t red = 50; ///obrót w osi Z to masakra do wyzerowania
  // Ustawienie jasności zależnie od Gyro_mix
  uint8_t brightness_Gyro = map(Gyro_mix, 0, 100, 50, 150); // dolny limit 50 by nie wygasić całkowicie

  fill_solid(leds, NUM_LEDS, CRGB(red, green, blue));
  FastLED.setBrightness(brightness_Gyro);
  FastLED.show();
}

///////////////////////////////////////////////////////////////
void rainbowEffect() 
{
  fill_rainbow(leds, NUM_LEDS, rainbowHue++, 7);
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}

////twinkleFOX
void twinkleFOX()
{
  fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = random(NUM_LEDS);
    leds[pos] = CRGB::White;
    FastLED.show();
}

///////CYLON
void cylonEffect()
{
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[cylonPos] = CRGB::Red;
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();

  cylonPos += cylonDir;
  if (cylonPos == NUM_LEDS - 1 || cylonPos == 0) cylonDir *= -1;
}

//////////////////////////////////////////////////////////////////////////////////
void PACIFICA_loop()
{
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(160 + sin8(pacificaWave + i * 10) / 4, 255, 100 + sin8(pacificaWave + i * 5) / 2);
  }
  pacificaWave++;
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}
void lavaLamp() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(sin8(millis() / 20 + i * 10), 255, 180);
  }
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}

void pulseFade() {
  if (pulseUp) {
    pulseBrightness += 5;
    if (pulseBrightness >= 255) pulseUp = false;
  } else {
    pulseBrightness -= 5;
    if (pulseBrightness <= 10) pulseUp = true;
  }
  fill_solid(leds, NUM_LEDS, CHSV(160, 255, pulseBrightness));
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}

void fireEffect() {
  for (int i = 0; i < NUM_LEDS; i++) {
    int flicker = random(120, 255);
    leds[i] = CRGB(flicker, flicker / 2, 0);
  }
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}

void colorWaves() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV((millis() / 10 + i * 8) % 255, 255, 255);
  }
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}