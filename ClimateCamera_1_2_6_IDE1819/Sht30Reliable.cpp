#include "Sht30Reliable.h"
#include <math.h>
#include <stdio.h>

// ===================== ПРЯМОЙ ПРОГРАММНЫЙ I2C ДРАЙВЕР SHT30 =====================
// Пины: SDA = GPIO 21, SCL = GPIO 22, адрес SHT30 = 0x44
// Работает через открытый сток (Open-Drain) с внутренней подтяжкой.
#define SHT30_SDA_PIN 21
#define SHT30_SCL_PIN 22
#define SHT30_ADDR    0x44
#define SHT30_HALF_PERIOD_US 10  // ~50 кГц — максимальная помехоустойчивость на длинном проводе

static inline void sht30SdaHigh() { pinMode(SHT30_SDA_PIN, INPUT_PULLUP); }
static inline void sht30SdaLow()  { digitalWrite(SHT30_SDA_PIN, LOW); pinMode(SHT30_SDA_PIN, OUTPUT); }

static inline void sht30SclHigh() {
  pinMode(SHT30_SCL_PIN, INPUT_PULLUP);
  uint32_t t0 = micros();
  while (digitalRead(SHT30_SCL_PIN) == LOW && (uint32_t)(micros() - t0) < 2000UL) {
    // Ожидание отпускания линии SCL (Clock Stretching)
  }
}
static inline void sht30SclLow()  { digitalWrite(SHT30_SCL_PIN, LOW); pinMode(SHT30_SCL_PIN, OUTPUT); }

static void sht30Start() {
  sht30SdaHigh(); sht30SclHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SdaLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SclLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
}

static void sht30Stop() {
  sht30SdaLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SclHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SdaHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
}

static bool sht30WriteByte(uint8_t data) {
  for (uint8_t i = 0; i < 8; ++i) {
    if (data & 0x80) sht30SdaHigh(); else sht30SdaLow();
    data <<= 1;
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    sht30SclHigh();
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    sht30SclLow();
  }
  sht30SdaHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SclHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  bool ack = (digitalRead(SHT30_SDA_PIN) == LOW);
  sht30SclLow();
  return ack;
}

static uint8_t sht30ReadByte(bool sendAck) {
  uint8_t data = 0;
  sht30SdaHigh();
  for (uint8_t i = 0; i < 8; ++i) {
    data <<= 1;
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    sht30SclHigh();
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    if (digitalRead(SHT30_SDA_PIN)) data |= 1;
    sht30SclLow();
  }
  if (sendAck) sht30SdaLow(); else sht30SdaHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SclHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sht30SclLow();  sht30SdaHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  return data;
}

uint8_t sht30Crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

bool sht30SendCmd(uint16_t cmd) {
  sht30Start();
  if (!sht30WriteByte((SHT30_ADDR << 1) | 0)) { sht30Stop(); return false; }
  if (!sht30WriteByte(cmd >> 8))              { sht30Stop(); return false; }
  if (!sht30WriteByte(cmd & 0xFF))            { sht30Stop(); return false; }
  sht30Stop();
  return true;
}

void initSHT30() {
  sht30SdaHigh();
  sht30SclHigh();
  delay(2);
  // Interface Reset (36 тактов SCL при SDA=HIGH) + Soft Reset
  for (int i = 0; i < 36; ++i) {
    sht30SclLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
    sht30SclHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
  }
  sht30Start();
  sht30Stop();
  delay(5);
  sht30SendCmd(0x30A2);
  delay(20);
}

bool readSHT30(float &outTemp, float &outHum) {
  if (!sht30SendCmd(0x2400)) {
    initSHT30();
    if (!sht30SendCmd(0x2400)) return false;
  }

  delay(18); // Ожидание замера высокой точности

  uint8_t b[6] = {0};
  bool gotAck = false;
  for (int attempt = 0; attempt < 5; ++attempt) {
    sht30Start();
    if (sht30WriteByte((SHT30_ADDR << 1) | 1)) {
      gotAck = true;
      break;
    }
    sht30Stop();
    delay(5);
  }
  if (!gotAck) {
    initSHT30();
    return false;
  }

  for (int i = 0; i < 6; ++i) {
    b[i] = sht30ReadByte(i < 5);
  }
  sht30Stop();

  if (sht30Crc8(&b[0], 2) != b[2] || sht30Crc8(&b[3], 2) != b[5]) {
    return false;
  }

  uint16_t rawT = ((uint16_t)b[0] << 8) | b[1];
  uint16_t rawH = ((uint16_t)b[3] << 8) | b[4];
  outTemp = -45.0f + 175.0f * ((float)rawT / 65535.0f);
  outHum  = 100.0f * ((float)rawH / 65535.0f);
  return true;
}
// ================================================================================

Sht30Reliable::Sht30Reliable(TwoWire &wire,uint8_t sda,uint8_t scl,uint8_t address)
  : bus(wire),pinSda(sda),pinScl(scl),addr(address) {}

void Sht30Reliable::sdaHigh() const { pinMode(pinSda, INPUT_PULLUP); }
void Sht30Reliable::sdaLow() const  { digitalWrite(pinSda, LOW); pinMode(pinSda, OUTPUT); }
void Sht30Reliable::sclHigh() const {
  pinMode(pinScl, INPUT_PULLUP);
  uint32_t t0 = micros();
  while (digitalRead(pinScl) == LOW && (uint32_t)(micros() - t0) < 2000UL) {}
}
void Sht30Reliable::sclLow() const  { digitalWrite(pinScl, LOW); pinMode(pinScl, OUTPUT); }

void Sht30Reliable::i2cStart() const {
  sdaHigh(); sclHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
  sdaLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sclLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
}

void Sht30Reliable::i2cStop() const {
  sdaLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sclHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
  sdaHigh(); delayMicroseconds(SHT30_HALF_PERIOD_US);
}

bool Sht30Reliable::i2cWriteByte(uint8_t data) const {
  for (uint8_t i = 0; i < 8; ++i) {
    if (data & 0x80) sdaHigh(); else sdaLow();
    data <<= 1;
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    sclHigh();
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    sclLow();
  }
  sdaHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sclHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  bool ack = (digitalRead(pinSda) == LOW);
  sclLow();
  return ack;
}

uint8_t Sht30Reliable::i2cReadByte(bool sendAck) const {
  uint8_t data = 0;
  sdaHigh();
  for (uint8_t i = 0; i < 8; ++i) {
    data <<= 1;
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    sclHigh();
    delayMicroseconds(SHT30_HALF_PERIOD_US);
    if (digitalRead(pinSda)) data |= 1;
    sclLow();
  }
  if (sendAck) sdaLow(); else sdaHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sclHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  sclLow(); sdaHigh();
  delayMicroseconds(SHT30_HALF_PERIOD_US);
  return data;
}

uint8_t Sht30Reliable::crc8(const uint8_t *data,size_t length) {
  return sht30Crc8(data, length);
}

const char *Sht30Reliable::errorName() const {
  switch(diag.error) {
    case Error::None: return "none";
    case Error::I2cBegin: return "i2c_begin";
    case Error::ClockLow: return "scl_stuck_low";
    case Error::DataLow: return "sda_stuck_low";
    case Error::WriteCommand: return "command_write";
    case Error::EndTransmission: return "end_transmission";
    case Error::ShortRead: return "short_read";
    case Error::TemperatureCrc: return "temperature_crc";
    case Error::HumidityCrc: return "humidity_crc";
    case Error::BothCrc: return "temperature_and_humidity_crc";
    case Error::StatusCrc: return "status_crc";
    case Error::StatusFlags: return "status_flags";
    case Error::ValueOutOfRange: return "value_out_of_range";
  }
  return "unknown";
}

void Sht30Reliable::formatError(char *out,size_t length) const {
  if(!out || !length) return;
  switch(diag.error) {
    case Error::None: snprintf(out,length,"нет ошибки"); break;
    case Error::I2cBegin: snprintf(out,length,"не удалось запустить I2C"); break;
    case Error::ClockLow: snprintf(out,length,"SCL удерживается в LOW"); break;
    case Error::DataLow: snprintf(out,length,"SDA удерживается в LOW"); break;
    case Error::WriteCommand:
      snprintf(out,length,"нет ACK команды 0x%04X",diag.command); break;
    case Error::EndTransmission:
      snprintf(out,length,"нет ответа SHT30 (0x%02X), команда 0x%04X",addr,diag.command); break;
    case Error::ShortRead:
      snprintf(out,length,"нет ACK при чтении (%u/%u байт)",diag.received,diag.expected); break;
    case Error::TemperatureCrc:
      snprintf(out,length,"неверная CRC температуры: 0x%02X, ожидалась 0x%02X",diag.crcReceived,diag.crcExpected); break;
    case Error::HumidityCrc:
      snprintf(out,length,"неверная CRC влажности: 0x%02X, ожидалась 0x%02X",diag.humCrcReceived,diag.humCrcExpected); break;
    case Error::BothCrc:
      snprintf(out,length,"неверная CRC температуры 0x%02X/0x%02X; неверная CRC влажности 0x%02X/0x%02X",
               diag.crcReceived,diag.crcExpected,diag.humCrcReceived,diag.humCrcExpected); break;
    case Error::StatusCrc:
      snprintf(out,length,"неверная CRC статуса: 0x%02X, ожидалась 0x%02X",diag.crcReceived,diag.crcExpected); break;
    case Error::StatusFlags:
      snprintf(out,length,"ошибка статуса SHT30: 0x%04X",diag.status); break;
    case Error::ValueOutOfRange:
      snprintf(out,length,"измерения вне допустимого диапазона"); break;
  }
}

bool Sht30Reliable::command(uint16_t value) {
  diag.command=value;
  i2cStart();
  if(!i2cWriteByte((uint8_t)((addr<<1)|0))) {
    i2cStop();
    diag.error=Error::EndTransmission;
    diag.wireCode=2; // NACK on address
    return false;
  }
  if(!i2cWriteByte((uint8_t)(value>>8)) || !i2cWriteByte((uint8_t)(value&0xFF))) {
    i2cStop();
    diag.error=Error::WriteCommand;
    return false;
  }
  i2cStop();
  return true;
}

bool Sht30Reliable::readBytes(uint8_t *data,size_t length) {
  diag.expected=(uint8_t)length;
  diag.received=0;
  bool gotAck=false;
  for(uint8_t attempt=0;attempt<5;++attempt) {
    i2cStart();
    if(i2cWriteByte((uint8_t)((addr<<1)|1))) {
      gotAck=true;
      break;
    }
    i2cStop();
    delay(2);
  }
  if(!gotAck) {
    diag.error=Error::ShortRead;
    return false;
  }
  for(size_t i=0;i<length;++i) {
    data[i]=i2cReadByte(i+1<length);
    diag.received=(uint8_t)(i+1);
  }
  i2cStop();
  return true;
}

bool Sht30Reliable::waitClockHigh() {
  uint32_t start=micros();
  while(digitalRead(pinScl)==LOW) {
    if((uint32_t)(micros()-start)>=2000UL) return false;
    delayMicroseconds(5);
  }
  return true;
}

bool Sht30Reliable::begin() { return recover(); }

bool Sht30Reliable::recover() {
  diag=Diagnostic();
  // Освобождаем аппаратный драйвер Wire, если он был активен, и переводим пины в Open-Drain
  bus.end();
  sdaHigh();
  sclHigh();
  delay(2);
  if(!waitClockHigh()) {
    diag.error=Error::ClockLow;
    return false;
  }
  // Interface Reset (36 тактов SCL при SDA=HIGH) для гарантированного сброса автомата SHT30
  for(uint8_t i=0;i<36;++i) {
    sclLow();  delayMicroseconds(SHT30_HALF_PERIOD_US);
    sclHigh();
    if(!waitClockHigh()) { diag.error=Error::ClockLow; return false; }
    delayMicroseconds(SHT30_HALF_PERIOD_US);
  }
  i2cStart();
  i2cStop();
  delay(5);
  if(digitalRead(pinSda)==LOW) {
    diag.error=Error::DataLow;
    return false;
  }
  // Soft Reset (0x30A2) + Clear Status (0x3041)
  if(!command(0x30A2)) return false;
  delay(15);
  command(0x3041);
  delay(2);
  return true;
}

bool Sht30Reliable::startMeasurement() {
  diag=Diagnostic();
  if(!command(0x2400)) {
    recover();
    if(!command(0x2400)) return false;
  }
  return true;
}

bool Sht30Reliable::readMeasurement(float &temperature,float &humidity) {
  diag=Diagnostic();
  uint8_t bytes[6]={0};
  if(!readBytes(bytes,sizeof(bytes))) return false;
  diag.crcExpected=crc8(bytes,2); diag.crcReceived=bytes[2];
  diag.humCrcExpected=crc8(bytes+3,2); diag.humCrcReceived=bytes[5];
  bool badT=diag.crcExpected!=diag.crcReceived;
  bool badH=diag.humCrcExpected!=diag.humCrcReceived;
  if(badT || badH) {
    diag.error=(badT && badH)?Error::BothCrc:(badT?Error::TemperatureCrc:Error::HumidityCrc);
    return false;
  }
  uint16_t rawT=((uint16_t)bytes[0]<<8)|bytes[1];
  uint16_t rawH=((uint16_t)bytes[3]<<8)|bytes[4];
  float t=-45.0f+175.0f*((float)rawT/65535.0f);
  float h=100.0f*((float)rawH/65535.0f);
  if(!isfinite(t) || !isfinite(h) || t<-40.0f || t>125.0f || h<0.0f || h>100.0f) {
    diag.error=Error::ValueOutOfRange; return false;
  }
  temperature=t; humidity=h;
  return true;
}
