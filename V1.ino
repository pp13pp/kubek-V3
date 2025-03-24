//#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <SPI.h>
#include <SD.h>
FASTLED_USING_NAMESPACE

#define DATA_PIN            2
#define NUM_LEDS            35
#define MAX_POWER_MILLIAMPS 1950
#define LED_TYPE            WS2812B
#define COLOR_ORDER         GRB
CRGB leds[NUM_LEDS];

/////// świecenie tryby

#define FRAMES_PER_SECOND 70
 bool gReverseDirection = false;

//bool overrideEffect = false;  // Flaga do kontroli efektu
enum EffectMode { PACIFICA, SOC_10, SOC_5, SOC_100, RAINBOW, flames, cylon, twinkle };  ///możliwe tryby
EffectMode currentMode = PACIFICA;
unsigned long overrideEndTime = 0;

int T_sygnalizacji=10000; ///czas bazowy sygnalizacji
int DELAYVAL=750; //czas bazowy wyświetlania efektów systemowych [ms]
unsigned long lastEffectTime = 0; 
bool SOC10Active = false; ///flaga do kontroli efektu świecenia przy SOC 10%
bool SOC5Active = false; ///flaga do kontroli efektu świecenia przy SOC 5%
bool SOC100Active = false; ///flaga do kontroli efektu świecenia przy SOC 99%

//////do obsługi żyroskopu
float lastAngleX = 0; ///poprzedni kąt nachylenia osi X
float lastAngleY = 0; ///poprzedni kąt nachylenia osi Y
float lastAngleZ = 0; ///poprzedni kąt nachylenia osi Z
float angleX=0; //
float angleY=0;
float angleZ=0;
float accX=0;
float accY=0;
float accZ=9.81;


Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

///////////ogólne i sprzętowe
#define laduj 0 //enable ładowania ogniw
#define wlancz 13 // pozycja włącznika ??? do zmiany przy SD

int pstryczek=1; // stan włącznika
int ladowanie=1; // 0 == nie ładuj; 1== żądanie ładowania
float termika=0; // czy jest bezpiecznie temicznie, 0==tak, >1 za ciepło
float temp1=20; //temp z ntc
int Ro = 10, B = 3950; //Nominal resistance 10K, Beta constant
float Rseries = 9.2;// R Series resistor 10K
float To = 298.15; // Nominal Temperature
float Vin=4.92; //napięcie "5V"

////////////ina219
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;

//////////////stan baterii
float SOC; // poziom naładowania %; 2,5V==0; 4,2V==100% 
double mAh; // poziom naładowania w mAh
float mAh1, mAh2; //pomocnicze
long prev_milis=0;
long czas=0; // czas na potrzeby liczenia
long czas_ladowania=0;
long czas_zasilZEW=0;

// D5, D6, D7, D8 // SCK, MISO, MOSI, CS
#define SD_CS_PIN D8

File dataFile; 
int obrot=0; //ilość pętli do zapisu mAh 
 
////////31 wartości do identyfikowania *początkowego* SOC
float napiecia[]={4.2, 4.06, 4.03, 3.99, 3.97, 3.94, 3.93, 3.91, 3.90, 3.89, 3.88, 3.8799, 3.8796, 3.873, 3.871, 3.870, 3.868, 3.867, 3.866, 3.865, 3.864, 3.862, 3.8638, 3.79, 3.43, 3.38, 3.14, 3.03, 2.937, 2.7, 2.4}; //progi napięc w V
float mAh_OCV[]={0,0.001,21.54,78.01, 152.30,229.56,321.693,452.45,586.18,722.88,868.49,1026.02,1141.91,1284.54,1415.36,1543.09,1685.73,1795.69,1899.782,2015.67,2122.58,2214.71,2312.77,2384.11,2443.53,2485.14,2523.74,2547.54,2556.463,2571.322,2575}; //progi pojemności mAh
int mAh_max=2575; //max SOC - zmierzyć kiedyś

void setup() {


// pixels.begin(); //start LED
delay( 2000); // 3 second delay for boot recovery, and a moment of silence
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection( TypicalLEDStrip );
  FastLED.setMaxPowerInVoltsAndMilliamps( 5, MAX_POWER_MILLIAMPS);

Serial.begin(9600);


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
for (int i = 0; i < 30; i = i + 1)
{
  if (busvoltage >= napiecia[i])
  {
  SOC=2575/mAh_OCV[i]*100; //% SOC
  mAh2=mAh_OCV[i]; //SOC mAh
  break;  //to mi wyskoczy z pętli for??
  }
}

mAh=((2*mAh1)+mAh2)/3; ///szacowanie SOC dla biednych
mAh=mAh*0.99;  ///odejmujemy za postój, kiedyś do poprawy na bazie modelu baterii

Wire.begin();   ///nie wiem czy to musi tu być
/////////żyroskop konfig
mpu.begin();


   mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  // Serial.print("Accelerometer range set to: ");
  switch (mpu.getAccelerometerRange()) {
  // case MPU6050_RANGE_2_G:
  //   // Serial.println("+-2G");
  //   break;
  case MPU6050_RANGE_4_G:
    // Serial.println("+-4G");
    break;
  // case MPU6050_RANGE_8_G:
  //   // Serial.println("+-8G");
  //   break;
  // case MPU6050_RANGE_16_G:
  //   // Serial.println("+-16G");
  //   break;
  }
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  // Serial.print("Gyro range set to: ");
  switch (mpu.getGyroRange()) {
  case MPU6050_RANGE_250_DEG:
    // Serial.println("+- 250 deg/s");
    break;
  // case MPU6050_RANGE_500_DEG:
  //   // Serial.println("+- 500 deg/s");
  //   break;
  // case MPU6050_RANGE_1000_DEG:
  //   // Serial.println("+- 1000 deg/s");
  //   break;
  // case MPU6050_RANGE_2000_DEG:
  //   // Serial.println("+- 2000 deg/s");
  //   break;
  }

  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);
  // Serial.print("Filter bandwidth set to: ");
  switch (mpu.getFilterBandwidth()) {
  // case MPU6050_BAND_260_HZ:
  //   // Serial.println("260 Hz");
  //   break;
  // case MPU6050_BAND_184_HZ:
  //   // Serial.println("184 Hz");
  //   break;
  // case MPU6050_BAND_94_HZ:
  //   // Serial.println("94 Hz");
  //   break;
  // case MPU6050_BAND_44_HZ:
  //   // Serial.println("44 Hz");
  //   break;
  case MPU6050_BAND_21_HZ:
    // Serial.println("21 Hz");
    break;
  // case MPU6050_BAND_10_HZ:
  //   // Serial.println("10 Hz");
  //   break;
  // case MPU6050_BAND_5_HZ:
  //   // Serial.println("5 Hz");
    // break;
  }

}

void loop() {
czas=millis()-prev_milis;  //czas wykonania pętli
prev_milis=czas; //zliczanie czasu pętli

/////////INA219
pomiary_ele();
obl_SOC();

//////////ładowanie
temp1= Read_NTC10k(); //temp z NTC
//Serial.println((String)"Temperature in celsius    :" + temp1 + "°C");
//Serial.println(" ");

//kolejność wykonywania ważna, najpierw żądania dbające os SOH ogniw, bezpieczeństwo termiczne tuż przed ładowaniem
tryb_muzeum(); //odpowiada za nie ładowanie permanente przy dłuższym postoju na zasilaniu zew.
term_bezp();
tryb_ladowania(); // odpowiada za ładowanie

////sygnalizacja niskiego stanu ogniw
bateria_sygnalizacja();

///////żyroskop
gyro();

/////wybierz tryb świecenia na podstawie akcji
swieciszTY();

if (millis() > overrideEndTime) currentMode = PACIFICA;
    
        if (millis() > overrideEndTime) currentMode = PACIFICA;
    
    if (SOC5Active) {
        SOC5();
    } else if (SOC10Active) {
        SOC10();
    } else {
        switch (currentMode) {
            case SOC_10: SOC10(); break;
            case SOC_5: SOC5(); break;
            case SOC_100: SOC100(); break;
            case RAINBOW: rainbow(); break;
            case flames:  Fire2012(); break;
            case cylon: cylon_(); break;
            case twinkle: twinkleFOX(); break;
            default: PACIFICA_loop(); break;
        }
    }

FastLED.show();
}


///////////////////////////////////////////////////////////////////////////////////////////////
void swieciszTY() //wybiera tryb świecenia na bazie tego co się akurat dzieje
{
  if (accZ > 12) 
  {
        currentMode = RAINBOW;
        overrideEndTime = millis() + 5000;
  } else if (fabs(angleX - lastAngleX) > 10) {
        currentMode = flames;
        overrideEndTime = millis() + 3000;
  } else if (fabs(accY) > 3) {
        currentMode = RAINBOW;
        overrideEndTime = millis() + 5000;
  }else if (fabs(angleY - lastAngleY) > 10) {
        currentMode = cylon;
        overrideEndTime = millis() + 3000;
  }else if (fabs(angleZ - lastAngleZ) > 10) {
        currentMode = twinkle;
        overrideEndTime = millis() + 3000;
  }
       

}
void tryb_ladowania() ///laduj==1 żądanie ładowania; ładuj == 0 nie ładujemy
{
pstryczek=digitalRead(wlancz);
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
  ladowanie=0;
  }
}
}

void tryb_muzeum()  /// jak stoisz na wystawie z muzeum to trzymaj tylko SOC w ryzach 40-80%, póki ktoś nie zarząda inaczej
{
  if (czas_zasilZEW>12*3600*1000) ///nie ładuj non stop
{
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

void pomiary_ele() ////pomiary elektryczne INA219
{
  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();     
  current_mA = ina219.getCurrent_mA();  // prąd ładowania / rozładowywania
  power_mW = ina219.getPower_mW();  //moc do liczenia Wh
  loadvoltage = busvoltage + (shuntvoltage / 1000); //napięcie pakietu 
}

void obl_SOC()  ///aktualne SOC baterii 
{
mAh=mAh+(current_mA*czas*1000/3600); //pojemność == poprzednia pojemność - prąd*czas w h (AKA Culomb counting)   /// upewnić się że prąd w + i * występuje i to w dobre strony

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
if(current_mA<0) ///upewnić się że prąd w - oznacza ładowanie
{
  if(laduj==0) ///jak masz prąd ładowania, a nie wystawiałeś sygnału do ładowania, daj zwrotkę o błędzie 
  {
  Serial.println((String)"ładujesz a nie powinieneś");
  Serial.println(" ");
  }

  czas_ladowania=czas_ladowania+czas; //zliczaj czas ładowania
  czas_zasilZEW=czas_zasilZEW+czas; //zliczaj czas na zasilaniu zew
  if(czas_ladowania>3*3600*1000) //czas ładowania powyżej 3h zeruje SOC
  {
    mAh=mAh_max; // "zeruj" SOC
  }
}

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

void gyro()///// gyroskop eventy
{
  lastAngleX = angleX; ///nadpisywanie kątów
  lastAngleY = angleY; ///nadpisywanie kątów
  lastAngleZ = angleZ; ///nadpisywanie kątów

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

///przyspieszenia w m/s2
 accX=a.acceleration.x;
 accY=a.acceleration.y;
 accZ=a.acceleration.z;      ///9,81 domyślne!

///prędkość kątowa rad/s
angleX=g.gyro.x*57.2958; // Konwersja rad/s na stopnie
angleY=g.gyro.y*57.2958; // Konwersja rad/s na stopnie
angleZ=g.gyro.z*57.2958; // Konwersja rad/s na stopnie

///temperatura w *C
// temp.temperature

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

float Read_NTC10k() /// czujnik temperatury -> dokładność kartofla
{
float Vi = analogRead(A0) * (Vin / 1023.0);
  //Convert voltage measured to resistance value
  //All Resistance are in kilo ohms.
  float R = ((Vin-Vi)*Rseries) / Vi;
  /*Use R value in steinhart and hart equation
    Calculate temperature value in kelvin*/
  float T =  1 / ((1 / To) + ((log(R / Ro)) / B));
  float tempC = T - 273.15-5; // Converting kelvin to celsius i dla tego egzemplarza -5st xD
  return tempC;
}



void bateria_sygnalizacja()  ///jak poziom SOC spada poniżej 10% zaczniej drzeć mordę
{
   if (millis() - lastEffectTime > T_sygnalizacji)
   {
        lastEffectTime = millis();
        
        if (mAh<mAh_max*0.05) //jest źle, gdzie mój prąd!
        {   SOC5Active = true;
            SOC10Active = false;
            SOC100Active = false;
        } else if (mAh<mAh_max*0.10) //dla 10% wstępne ostrzeżenie
        {   SOC10Active = true;
            SOC5Active = false;
            SOC100Active = false;
        } else if (mAh>mAh_max*0.99 && czas_ladowania>1*3600*1000 && czas_ladowania>6*3600*1000)  //jak masz powyżej 99% podczał ładowania powyżej 1h i poniżej 5h
        { SOC100Active = true;
          SOC5Active = false;
          SOC10Active = false;
        }else
        {   SOC10Active = false;
            SOC5Active = false;
            SOC100Active = false;
        }

    }

  if (SOC5Active && millis() - lastEffectTime > DELAYVAL*2)
  {
    SOC5Active = false;
  }
    
  if (SOC10Active && millis() - lastEffectTime > DELAYVAL)
  {
    SOC10Active = false;
  }
    if (SOC100Active && millis() - lastEffectTime > DELAYVAL*2)
  {
    SOC100Active = false;
  }

}

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

///////////////////////////////////////////////warunkowane SOC
void SOC10()  // jak SOC spadnie poniżej 10%
{
        FastLED.clear(); //zgaś wszystko
        FastLED.show();
        fill_solid(leds, NUM_LEDS, CRGB(76, 0, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
}

void SOC5() // jak SOC spadnie poniżej 5%
{
        FastLED.clear(); //zgaś wszystko
        FastLED.show();
        for(int i=0; i<NUM_LEDS-5; i=i+5) //co 5 zapal na czerwono
        {
        leds[i] = CRGB(150, 0, 0); ///napierdalaj czerowym
        }
}
void SOC100()  // jak SOC powyżej 99% podczas ładowania
{
        fill_solid(leds, NUM_LEDS, CRGB(0, 85, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
        FastLED.clear(); //zgaś wszystko
        FastLED.show();
        delay(DELAYVAL/2);
        fill_solid(leds, NUM_LEDS, CRGB(0, 85, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
}
///////////////////////////////////////////////////////////////

void rainbow() {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV((i * 10) % 255, 255, 250);  ///ostatnia wartość to jasność
    }
}

////twinkleFOX
void twinkleFOX()
{
  fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = random(NUM_LEDS);
    leds[pos] = CRGB::White;
}

///////CYLON
void cylon_()
{
  static uint8_t hue = 0;
    for(int i = 0; i < NUM_LEDS; i++) {
        // Set the i'th led to red 
        leds[i] = CHSV(hue++, 255, 255);
        // Show the leds
        FastLED.show(); 
        // now that we've shown the leds, reset the i'th led to black
        // leds[i] = CRGB::Black;
        fadeall();
        // Wait a little bit before we loop around and do it again
        delay(10);
    }
   
      // Now go in the other direction.  
    for(int i = (NUM_LEDS)-1; i >= 0; i--) {
        // Set the i'th led to red 
        leds[i] = CHSV(hue++, 255, 255);
        // Show the leds
        FastLED.show();
        // now that we've shown the leds, reset the i'th led to black
        // leds[i] = CRGB::Black;
        fadeall();
        // Wait a little bit before we loop around and do it again
        delay(10);
    }
}
void fadeall() 
{ 
for(int i = 0; i < NUM_LEDS; i++)
{ leds[i].nscale8(250); } 
}

//////////////flames
#define COOLING  55
#define SPARKING 120
void Fire2012()
{
  // Array of temperature readings at each simulation cell
  static uint8_t heat[NUM_LEDS];
 
  // Step 1.  Cool down every cell a little
    for( int i = 0; i < NUM_LEDS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }
  
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= NUM_LEDS - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }
    
    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160,255) );
    }
 
    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < NUM_LEDS; j++) {
      CRGB color = HeatColor( heat[j]);
      int pixelnumber;
      if( gReverseDirection ) {
        pixelnumber = (NUM_LEDS-1) - j;
      } else {
        pixelnumber = j;
      }
      leds[pixelnumber] = color;
    }
    FastLED.delay(1000 / FRAMES_PER_SECOND);
}

//////////////////////////////////////////////////////////////////////////////////
void PACIFICA_loop()
{// Increment the four "color index start" counters, one for each wave layer.
  // Each is incremented at a different speed, and the speeds vary over time.
  static uint16_t sCIStart1, sCIStart2, sCIStart3, sCIStart4;
  static uint32_t sLastms = 0;
  uint32_t ms = GET_MILLIS();
  uint32_t deltams = ms - sLastms;
  sLastms = ms;
  uint16_t speedfactor1 = beatsin16(3, 179, 269);
  uint16_t speedfactor2 = beatsin16(4, 179, 269);
  uint32_t deltams1 = (deltams * speedfactor1) / 256;
  uint32_t deltams2 = (deltams * speedfactor2) / 256;
  uint32_t deltams21 = (deltams1 + deltams2) / 2;
  sCIStart1 += (deltams1 * beatsin88(1011,10,13));
  sCIStart2 -= (deltams21 * beatsin88(777,8,11));
  sCIStart3 -= (deltams1 * beatsin88(501,5,7));
  sCIStart4 -= (deltams2 * beatsin88(257,4,6));

  // Clear out the LED array to a dim background blue-green
  fill_solid( leds, NUM_LEDS, CRGB( 2, 6, 10));

CRGBPalette16 pacifica_palette_1 = 
    { 0x000507, 0x000409, 0x00030B, 0x00030D, 0x000210, 0x000212, 0x000114, 0x000117, 
      0x000019, 0x00001C, 0x000026, 0x000031, 0x00003B, 0x000046, 0x14554B, 0x28AA50 };
CRGBPalette16 pacifica_palette_2 = 
    { 0x000507, 0x000409, 0x00030B, 0x00030D, 0x000210, 0x000212, 0x000114, 0x000117, 
      0x000019, 0x00001C, 0x000026, 0x000031, 0x00003B, 0x000046, 0x0C5F52, 0x19BE5F };
CRGBPalette16 pacifica_palette_3 = 
    { 0x000208, 0x00030E, 0x000514, 0x00061A, 0x000820, 0x000927, 0x000B2D, 0x000C33, 
      0x000E39, 0x001040, 0x001450, 0x001860, 0x001C70, 0x002080, 0x1040BF, 0x2060FF };
      
  // Render each of four layers, with different scales and speeds, that vary over time
  pacifica_one_layer( pacifica_palette_1, sCIStart1, beatsin16( 3, 11 * 256, 14 * 256), beatsin8( 10, 70, 130), 0-beat16( 301) );
  pacifica_one_layer( pacifica_palette_2, sCIStart2, beatsin16( 4,  6 * 256,  9 * 256), beatsin8( 17, 40,  80), beat16( 401) );
  pacifica_one_layer( pacifica_palette_3, sCIStart3, 6 * 256, beatsin8( 9, 10,38), 0-beat16(503));
  pacifica_one_layer( pacifica_palette_3, sCIStart4, 5 * 256, beatsin8( 8, 10,28), beat16(601));

  // Add brighter 'whitecaps' where the waves lines up more
  pacifica_add_whitecaps();

  // Deepen the blues and greens a bit
  pacifica_deepen_colors();
}
// Add one layer of waves into the led array
void pacifica_one_layer( CRGBPalette16& p, uint16_t cistart, uint16_t wavescale, uint8_t bri, uint16_t ioff)
{
  uint16_t ci = cistart;
  uint16_t waveangle = ioff;
  uint16_t wavescale_half = (wavescale / 2) + 20;
  for( uint16_t i = 0; i < NUM_LEDS; i++) {
    waveangle += 250;
    uint16_t s16 = sin16( waveangle ) + 32768;
    uint16_t cs = scale16( s16 , wavescale_half ) + wavescale_half;
    ci += cs;
    uint16_t sindex16 = sin16( ci) + 32768;
    uint8_t sindex8 = scale16( sindex16, 240);
    CRGB c = ColorFromPalette( p, sindex8, bri, LINEARBLEND);
    leds[i] += c;
  }
}
// Add extra 'white' to areas where the four layers of light have lined up brightly
void pacifica_add_whitecaps()
{
  uint8_t basethreshold = beatsin8( 9, 55, 65);
  uint8_t wave = beat8( 7 );
  
  for( uint16_t i = 0; i < NUM_LEDS; i++) {
    uint8_t threshold = scale8( sin8( wave), 20) + basethreshold;
    wave += 7;
    uint8_t l = leds[i].getAverageLight();
    if( l > threshold) {
      uint8_t overage = l - threshold;
      uint8_t overage2 = qadd8( overage, overage);
      leds[i] += CRGB( overage, overage2, qadd8( overage2, overage2));
    }
  }
}
// Deepen the blues and greens
void pacifica_deepen_colors()
{
  for( uint16_t i = 0; i < NUM_LEDS; i++) {
    leds[i].blue = scale8( leds[i].blue,  145); 
    leds[i].green= scale8( leds[i].green, 200); 
    leds[i] |= CRGB( 2, 5, 7);
  }
}