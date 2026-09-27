#include <BlinkyPicoW.h>
#include <SPI.h>

// --- Configuration Constants ---
constexpr int BLINKY_DIAG    = 0;
constexpr int CUBE_DIAG      = 0;
constexpr int COMM_LED_PIN   = 2;
constexpr int RST_BUTTON_PIN = 3;
constexpr int NUMCHAN        = 3;

constexpr int CS_PINS[NUMCHAN] = {17, 20, 21};

// --- Data Structures ---
struct CubeSetting 
{
  uint32_t publishInterval;
};

struct CubeReading 
{
  float temp[NUMCHAN];
};

struct CubeArm 
{
  bool temp[NUMCHAN];
};

// --- Global Variables ---
CubeSetting setting;
CubeReading reading;
CubeReading readingLow;
CubeReading readingHigh;
CubeArm readingArm;

uint32_t lastPublishTime;
SPISettings spiSettings;

// --- Helper Functions ---
template <typename T>
inline bool outsideLimits(T current, T low, T high) 
{
  return (current < low) || (current > high);
}

// --- Setup Functions ---
void setupBlinky() 
{
  if (BLINKY_DIAG > 0) {Serial.begin(9600);}

  // MQTT & Hardware Configuration
  BlinkyPicoW.setMqttKeepAlive(15);
  BlinkyPicoW.setMqttSocketTimeout(4);
  BlinkyPicoW.setMqttPort(1883);
  BlinkyPicoW.setMqttLedFlashMs(100);
  BlinkyPicoW.setHdwrWatchdogMs(8000);

  BlinkyPicoW.begin(BLINKY_DIAG, COMM_LED_PIN, RST_BUTTON_PIN, true, sizeof(setting), sizeof(reading));
}

void setupCube() {
  if (BLINKY_DIAG < 1 && CUBE_DIAG > 0) {Serial.begin(9600);}
  setting.publishInterval = 4000;
  lastPublishTime = millis();

  for (int i = 0; i < NUMCHAN; ++i) 
  {
    reading.temp[i] = 0.0;
    readingArm.temp[i] = true;
    pinMode(CS_PINS[i], OUTPUT);
    digitalWrite(CS_PINS[i], HIGH);
  }

  spiSettings = SPISettings(2000000, MSBFIRST, SPI_MODE1);
  SPI.begin();

}

void loopCube()
{
  const unsigned long now = millis();
  if ((now - lastPublishTime) > setting.publishInterval)
  {
    lastPublishTime = now;
    if (BlinkyPicoW.publishCubeData(reinterpret_cast<uint8_t*>(&setting), reinterpret_cast<uint8_t*>(&reading), false)) 
    {
      for (int i = 0; i < NUMCHAN; ++i) 
      {
        if (!outsideLimits(reading.temp[i], readingLow.temp[i], readingHigh.temp[i])) 
        {
          readingArm.temp[i] = true;
        }
      }
    }  
  }
  for (int i = 0; i < NUMCHAN; ++i) 
  {
    reading.temp[i] = getMAX31855Temperature(CS_PINS[i], spiSettings);
    if (BlinkyPicoW.isInitialized())
    {  
      if (outsideLimits(reading.temp[i], readingLow.temp[i], readingHigh.temp[i])) 
      {
        if (readingArm.temp[i]) 
        {
          const bool published = BlinkyPicoW.publishCubeData(
            reinterpret_cast<uint8_t*>(&setting), 
            reinterpret_cast<uint8_t*>(&reading), 
            true
          );
          
          readingArm.temp[i] = !published;
          if (published) 
          {
            lastPublishTime = now;
          }
        }
      }
    }
  }
  // Check for New MQTT Settings
  const bool newSettings = BlinkyPicoW.retrieveCubeSetting(
    reinterpret_cast<uint8_t*>(&setting), 
    reinterpret_cast<uint8_t*>(&readingLow), 
    reinterpret_cast<uint8_t*>(&readingHigh)
  );
  if (newSettings) 
  {
    if (setting.publishInterval < 1000) setting.publishInterval = 500;
  }  
}
float getMAX31855Temperature(int chipSelect, SPISettings spiSetup)
{
  uint8_t  dataBufRead[4];
  int bits[32];
  int iTemp = 0;
  int pow2 = 1; 

  SPI.beginTransaction(spiSetup);
  delay(10);
  digitalWrite (chipSelect, LOW);
  delay(10);
  SPI.transfer(&dataBufRead, 4);
  delay(10);
  digitalWrite (chipSelect, HIGH);
  delay(10);
  SPI.endTransaction();

  for (int ibyte = 0; ibyte < 4; ++ibyte)
  {
    for (int ibit = 0; ibit < 8; ++ibit)
    {
      bits[31 - (ibyte * 8 + 7 - ibit)] = ((dataBufRead[ibyte] >> ibit) % 2);
    }
  }
  iTemp = 0;
  pow2 = 1;
  for (int ibit = 18; ibit < 31; ++ibit)
  {
    iTemp = iTemp + pow2 * bits[ibit];
    pow2 = pow2 * 2;
  }
  if (bits[31] > 0)
  {
    iTemp = iTemp - 8192;
  }
  float ftemp = (((float) iTemp) - 2) / 4;
  return ftemp;
}
