#include <bluefruit.h>
#include <time.h>
#include <Adafruit_NeoPixel.h> 
#include <cQueue.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif
#define PIN 30
#define ANIMATION_PERIOD_MS  300

class timer {
  private:
    unsigned long begTime;
    bool running = false;
    unsigned long end = 0;
  public:
    void start() {
      begTime = clock();
      running = true;
    }

    unsigned long elapsedTime() {
      return ((unsigned long) clock() - begTime) / CLOCKS_PER_SEC;
    }

    bool isTimeout(unsigned long seconds) {
      return seconds >= elapsedTime();
    }

    void stop() {
      if(running) {
        end = (unsigned long) clock();
        running = false;
      }
    }    
};

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(11, PIN, NEO_GRB + NEO_KHZ800);
BLEUart bleuart;

// Function prototypes for packetparser.cpp
uint8_t readPacket (BLEUart *ble_uart, uint16_t timeout);
float   parsefloat (uint8_t *buffer);
void    printHex   (const uint8_t * data, const uint32_t numBytes);

// Packet buffer
extern uint8_t packetbuffer[];
int r;
int g;
int b;

//New Queue Item
int newLevel;
int newTime;

//Timer
timer t;
int deltaT;
int wTime;
int hTime;
int tTime;

//Level
int wLevel;
int hLevel;
int tLevel;

//bool
bool isTime;
bool initialLoop = true;
bool hasPoppedW;
bool hasPoppedH;
bool hasPoppedT;

//Settings
int currentMode = 3;
int defaultMode;
int weather[9];
int temperature[30];
int humidity[15];

typedef struct strItem {
  int level;
  int timeVal;
} Item;

//Queue counter
int wCount = 0;
int hCount = 0;
int tCount = 0;

Queue_t wSchedule; // Weather Queue declaration
Queue_t hSchedule; // Humidity Queue declaration
Queue_t tSchedule; // Temperature Queue declaration

void setup(void)
{
  Serial.begin(19200);
  Serial.println(F("Adafruit Bluefruit52 WIND Control"));
  strip.begin();
  strip.show();
  Bluefruit.begin();
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
  Bluefruit.setTxPower(4);
  Bluefruit.setName("WIND Bracelet");
  q_init(&wSchedule, sizeof(Item), 10, FIFO, true);
  q_init(&hSchedule, sizeof(Item), 10, FIFO, true);
  q_init(&tSchedule, sizeof(Item), 10, FIFO, true);

  // Configure and start the BLE Uart service
  bleuart.begin();
  // Set up and start advertising
  startAdv();
}

void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  
  // Include the BLE UART (AKA 'NUS') 128-bit UUID
  Bluefruit.Advertising.addService(bleuart);

  // Secondary Scan Response packet (optional)
  // Since there is no room for 'Name' in Advertising packet
  Bluefruit.ScanResponse.addName();

  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
   
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

/**************************************************************************/
/*!
    @brief  Constantly poll for new command or response data
*/
/**************************************************************************/
void loop() {
  //Start timer if schedule Item exists
  if (initialLoop){
    t.start();
    initialLoop = false;
    isTime = true;
    popAllSchedule();
    Serial.print("TIME START");
  }

  //Calculate Time change on all popped queue items and set RGB
  calcTime();
  
  //Change to correct mode if new mode packet came last loop
  getMode();
  
  //Display correct values for this loop
  animatePixels(strip, r, g, b, currentMode);
  
  // Wait for new data to arrive
  uint8_t len = readPacket(&bleuart, 500);
  if (len == 0) return;

// Parse data
  if (packetbuffer[1] == 'M') {
    Serial.print("PARSE M");
    defaultMode = packetbuffer[2];
  }else if (packetbuffer[1] == 'S'){
    Serial.print("PARSE S");
    //Handle adding item to correct queue
    switch(packetbuffer[2]){
      //Weather Schedule Item
      case 0: newLevel = packetbuffer[3];
              newTime = packetbuffer[4];
              Item wItem;
              wItem.level = newLevel;
              wItem.timeVal = newTime * 60;
              q_push(&wSchedule, &wItem);
              hasPoppedW = true;
              break;
      //Humidity Schedule Item
      case 1: newLevel = packetbuffer[3];
              newTime = packetbuffer[4];
              Item hItem;
              hItem.level = newLevel;
              hItem.timeVal = newTime * 60;
              q_push(&hSchedule, &hItem);
              hasPoppedH = true;
              break;
      //Temperature Schedule Item
      case 2: newLevel = packetbuffer[3];
              newTime = packetbuffer[4];
              Item tItem;
              tItem.level = newLevel;
              tItem.timeVal = newTime * 60;
              q_push(&tSchedule, &tItem);
              hasPoppedT = true;
              break;
      
    } 
  }else if (packetbuffer[1] == 'W') {
    Serial.print("PARSE W SETTING");
    
    switch(packetbuffer[2]){
      case '0': weather[0] = packetbuffer[3];
              weather[1] = packetbuffer[4];
              weather[2] = packetbuffer[5];
              Serial.print("WROTE SUNNY ");
              break;
              
     case '1':  weather[3] = packetbuffer[3];
              weather[4] = packetbuffer[4];
              weather[5] = packetbuffer[5];
              break;
              
     case '2':  weather[6] = packetbuffer[3];
              weather[7] = packetbuffer[4];
              weather[8] = packetbuffer[5];
              break;
    }
  }else if (packetbuffer[1] == 'H') {
    Serial.print("PARSE H SETTING");
    switch(packetbuffer[2]){
    case '0':   humidity[0] = packetbuffer[3];
              humidity[1] = packetbuffer[4];
              humidity[2] = packetbuffer[5];
              break;
              
    case '1':   humidity[3] = packetbuffer[3];
              humidity[4] = packetbuffer[4];
              humidity[5] = packetbuffer[5];
              break;
              
    case '2':   humidity[6] = packetbuffer[3];
              humidity[7] = packetbuffer[4];
              humidity[8] = packetbuffer[5];
              break;
              
    case '3':   humidity[9] = packetbuffer[3];
              humidity[10] = packetbuffer[4];
              humidity[11] = packetbuffer[5];
              break;
              
     case '4':  humidity[12] = packetbuffer[3];
              humidity[13] = packetbuffer[4];
              humidity[14] = packetbuffer[5];
              break;
    }          
  }else if (packetbuffer[1]== 'T') {
    Serial.print("PARSE T SETTING");
    switch(packetbuffer[2]){
    case '0':   temperature[0] = packetbuffer[3];
              temperature[1] = packetbuffer[4];
              temperature[2] = packetbuffer[5];
              break;
              
    case '1':   temperature[3] = packetbuffer[3];
              temperature[4] = packetbuffer[4];
              temperature[5] = packetbuffer[5];
              break;
              
    case '2':   temperature[6] = packetbuffer[3];
              temperature[7] = packetbuffer[4];
              temperature[8] = packetbuffer[5];
              break;
              
    case '3':   temperature[9] = packetbuffer[3];
              temperature[10] = packetbuffer[4];
              temperature[11] = packetbuffer[5];
              break;
              
    case '4':   temperature[12] = packetbuffer[3];
              temperature[13] = packetbuffer[4];
              temperature[14] = packetbuffer[5];
              break;
              
    case '5':   temperature[15] = packetbuffer[3];
              temperature[16] = packetbuffer[4];
              temperature[17] = packetbuffer[5];
              break;
              
    case '6':   temperature[18] = packetbuffer[3];
              temperature[19] = packetbuffer[4];
              temperature[20] = packetbuffer[5];
              break;
              
     case '7':  temperature[21] = packetbuffer[3];
              temperature[22] = packetbuffer[4];
              temperature[23] = packetbuffer[5];
              break;
              
     case '8':  temperature[24] = packetbuffer[3];
              temperature[25] = packetbuffer[4];
              temperature[26] = packetbuffer[5];
              break;
              
     case '9':  temperature[27] = packetbuffer[3];
              temperature[28] = packetbuffer[4];
              temperature[29] = packetbuffer[5];
              break;
  }
 }
}

void calcTime() {
  deltaT = t.elapsedTime() + 3;
  if(hasPoppedW == true){
    //Weather time calc
    wTime = wTime - deltaT;
    Serial.print(wTime);
    Serial.print(" Time left minus ");
    Serial.print(deltaT);
    if(wTime <= 0){
      hasPoppedW = false;
      Serial.print("0 time popping next");
      popNextW();
    }else Serial.print("Time still valid");
  }if(hasPoppedH == true){
    //Humidity time calc
    hTime = hTime - deltaT;
    if(hTime <= 0){
      hasPoppedH = false;
      Serial.print("0 time popping next");
      popNextH();
    }else Serial.print("Not 0 time ");
  }if(hasPoppedT == true){
    //Temperature time calc
    tTime = tTime - deltaT;
    if(tTime <= 0){
      hasPoppedT = false;
      Serial.print("0 time popping next");
      popNextT();
    }else Serial.print("Not 0 time ");
  }
}

void popNextW(){
  if(q_nbRecs(&wSchedule) > 0){
    Serial.print("Queue not empty");
    Item currentWItem;
    q_pop(&wSchedule, &currentWItem);
    wLevel = currentWItem.level;
    wTime = currentWItem.timeVal;
    hasPoppedW = true;
  }else defaultMode = 3;
}

void popNextH(){
  if(q_nbRecs(&hSchedule) > 0){
    Item currentHItem;
    q_pop(&hSchedule, &currentHItem);
    hLevel = currentHItem.level;
    hTime = currentHItem.timeVal;
    hasPoppedH = true;
  }else defaultMode = 3;
}

void popNextT(){
  if(q_nbRecs(&tSchedule) > 0){
    Item currentTItem;
    q_pop(&tSchedule, &currentTItem);
    tLevel = currentTItem.level;
    tTime = currentTItem.timeVal;
    hasPoppedT = true;
  }else defaultMode = 3;
}

int getMode() {
  currentMode = defaultMode;
  return currentMode;
}

void popAllSchedule(){
  popNextW();
  popNextH();
  popNextT();
  if (!hasPoppedW && !hasPoppedH && !hasPoppedT) defaultMode = 3;
}

void animatePixels(Adafruit_NeoPixel& strip, uint8_t r, uint8_t g, uint8_t b, int currentMode) {
  switch (currentMode){
    case 0 : //weather
              if(hasPoppedW){
              r = weather[(wLevel * 3)];
              g = weather[(wLevel * 3) + 1];
              b = weather[(wLevel * 3) + 2];
              colorWipe(strip.Color(r, g, b), 500);
              }
              break;
    case 1 : //humidity
              if(hasPoppedH){
              r = humidity[hLevel * 3];
              g = humidity[(hLevel * 3) + 1];
              b = humidity[(hLevel * 3) + 2];
              colorWipe(strip.Color(r, g, b), 500);
              Serial.print("Animate Humidity");
              }
              break;
    case 2 : //temperature
              if(hasPoppedT){
              r = temperature[tLevel * 3];
              g = temperature[(tLevel * 3) + 1];
              b = temperature[(tLevel * 3) + 2];
              colorWipe(strip.Color(r, g, b), 500);
              Serial.print("Animate Temperature");
              }
              break;
              //Standby
    case 3 :  colorWipe(strip.Color(255, 255, 255), 500);
              break;
  }
}


void rainbow(uint8_t wait) {
  uint16_t i, j;

  for(j=0; j<256; j++) {
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i+j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void changeColor(uint32_t c) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.show();
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}