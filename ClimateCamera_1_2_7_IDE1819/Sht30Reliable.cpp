#include "Sht30Reliable.h"
#include <math.h>
#include <stdio.h>

// Команды датчика GY-21 (HTU21D / SHT21)
static constexpr uint8_t HTU21_CMD_TRIG_TEMP_NOHOLD = 0xF3;
static constexpr uint8_t HTU21_CMD_TRIG_HUM_NOHOLD  = 0xF5;
static constexpr uint8_t HTU21_CMD_WRITE_USER_REG   = 0xE6;
static constexpr uint8_t HTU21_CMD_READ_USER_REG    = 0xE7;
static constexpr uint8_t HTU21_CMD_SOFT_RESET       = 0xFE;

Sht30Reliable::Sht30Reliable(TwoWire &wire,uint8_t sda,uint8_t scl,uint8_t address)
  : bus(wire),pinSda(sda),pinScl(scl),addr(address) {}

// CRC-8 для HTU21D / SHT21: полином 0x31 (x^8 + x^5 + x^4 + 1), начальное значение 0x00
uint8_t Sht30Reliable::crc8(const uint8_t *data,size_t length) {
  uint8_t crc=0x00;
  for(size_t i=0;i<length;++i) {
    crc^=data[i];
    for(uint8_t bit=0;bit<8;++bit)
      crc=(crc&0x80)?(uint8_t)((crc<<1)^0x31):(uint8_t)(crc<<1);
  }
  return crc;
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
      snprintf(out,length,"ошибка записи команды 0x%02X",diag.command); break;
    case Error::EndTransmission:
      snprintf(out,length,"нет ответа GY-21 (0x%02X): код I2C %u, команда 0x%02X",addr,diag.wireCode,diag.command); break;
    case Error::ShortRead:
      snprintf(out,length,"получено меньше %u байт от GY-21: %u/%u",diag.expected,diag.received,diag.expected); break;
    case Error::TemperatureCrc:
      snprintf(out,length,"неверная CRC температуры GY-21: 0x%02X, ожидалась 0x%02X",diag.crcReceived,diag.crcExpected); break;
    case Error::HumidityCrc:
      snprintf(out,length,"неверная CRC влажности GY-21: 0x%02X, ожидалась 0x%02X",diag.humCrcReceived,diag.humCrcExpected); break;
    case Error::BothCrc:
      snprintf(out,length,"неверная CRC T 0x%02X/0x%02X и RH 0x%02X/0x%02X",
               diag.crcReceived,diag.crcExpected,diag.humCrcReceived,diag.humCrcExpected); break;
    case Error::StatusCrc:
      snprintf(out,length,"ошибка чтения регистра статуса GY-21"); break;
    case Error::StatusFlags:
      snprintf(out,length,"ошибка статуса/питания GY-21: 0x%02X",diag.status); break;
    case Error::ValueOutOfRange:
      snprintf(out,length,"измерения GY-21 вне допустимого диапазона"); break;
  }
}

bool Sht30Reliable::command8(uint8_t value) {
  diag.command=value;
  bus.beginTransmission(addr);
  size_t written=bus.write(value);
  uint8_t error=bus.endTransmission(true);
  if(error) { diag.error=Error::EndTransmission; diag.wireCode=error; return false; }
  if(written!=1) { diag.error=Error::WriteCommand; return false; }
  return true;
}

bool Sht30Reliable::writeUserReg(uint8_t value) {
  diag.command=HTU21_CMD_WRITE_USER_REG;
  bus.beginTransmission(addr);
  size_t w1=bus.write(HTU21_CMD_WRITE_USER_REG);
  size_t w2=bus.write(value);
  uint8_t error=bus.endTransmission(true);
  if(error) { diag.error=Error::EndTransmission; diag.wireCode=error; return false; }
  if(w1!=1 || w2!=1) { diag.error=Error::WriteCommand; return false; }
  return true;
}

bool Sht30Reliable::readBytes(uint8_t *data,size_t length) {
  size_t received=bus.requestFrom(addr,length,true);
  diag.expected=(uint8_t)length;
  diag.received=(uint8_t)received;
  if(received!=length) {
    while(bus.available()) bus.read();
    diag.error=Error::ShortRead;
    return false;
  }
  for(size_t i=0;i<length;++i) {
    if(!bus.available()) {
      diag.received=(uint8_t)i;
      diag.error=Error::ShortRead;
      return false;
    }
    data[i]=(uint8_t)bus.read();
  }
  return true;
}

bool Sht30Reliable::waitClockHigh() {
  uint32_t start=micros();
  while(digitalRead(pinScl)==LOW) {
    if((uint32_t)(micros()-start)>=1000UL) return false;
    delayMicroseconds(5);
  }
  return true;
}

bool Sht30Reliable::begin() { return recover(); }

bool Sht30Reliable::recover() {
  diag=Diagnostic();
  // Освобождение зависшего ведомого на шине I2C (9 импульсов SCL + STOP в режиме Open-Drain)
  bus.end();
  digitalWrite(pinSda,HIGH); pinMode(pinSda,OUTPUT_OPEN_DRAIN);
  digitalWrite(pinScl,HIGH); pinMode(pinScl,OUTPUT_OPEN_DRAIN);
  bool released=waitClockHigh();
  if(released) {
    for(uint8_t pulse=0;pulse<9 && digitalRead(pinSda)==LOW;++pulse) {
      digitalWrite(pinScl,LOW); delayMicroseconds(5);
      digitalWrite(pinScl,HIGH);
      if(!waitClockHigh()) { released=false; break; }
      delayMicroseconds(5);
    }
  }
  if(released) {
    digitalWrite(pinScl,LOW); digitalWrite(pinSda,LOW); delayMicroseconds(5);
    digitalWrite(pinScl,HIGH);
    released=waitClockHigh();
    delayMicroseconds(5); digitalWrite(pinSda,HIGH); delayMicroseconds(5);
    released=released && digitalRead(pinSda)==HIGH;
  }
  bool clockLow=digitalRead(pinScl)==LOW;
  digitalWrite(pinSda,HIGH); digitalWrite(pinScl,HIGH);
  pinMode(pinSda,INPUT_PULLUP); pinMode(pinScl,INPUT_PULLUP);
  if(!bus.begin(pinSda,pinScl,100000)) { diag.error=Error::I2cBegin; return false; }
  bus.setClock(100000); bus.setTimeOut(30);
  if(!released) { diag.error=clockLow?Error::ClockLow:Error::DataLow; return false; }

  // Мягкий сброс датчика GY-21 (HTU21D)
  if(!command8(HTU21_CMD_SOFT_RESET)) return false;
  delay(15);

  // Чтение регистра пользователя (0xE7) для проверки связи и контроля питания
  if(!command8(HTU21_CMD_READ_USER_REG)) return false;
  uint8_t userReg=0;
  if(!readBytes(&userReg,1)) return false;
  diag.status=userReg;
  // Бит 6 (0x40) сигнализирует о низком напряжении питания VDD < 2.25V
  if(userReg & 0x40) { diag.error=Error::StatusFlags; return false; }

  // Разрешение 12 бит RH / 14 бит Temp (биты 7 и 0 = 0), нагреватель выключен (бит 2 = 0)
  uint8_t cleanReg = (userReg & 0x38) | 0x02; // сохраняем резервные биты 3..5 и OTP reload disable
  if(!writeUserReg(cleanReg)) return false;

  return true;
}

bool Sht30Reliable::startMeasurement() {
  diag=Diagnostic();
  // Запускаем измерение температуры в режиме No-Hold Master (0xF3)
  return command8(HTU21_CMD_TRIG_TEMP_NOHOLD);
}

bool Sht30Reliable::readMeasurement(float &temperature,float &humidity) {
  diag=Diagnostic();

  // 1. Читаем 3 байта температуры (2 байта данных + 1 байт CRC-8)
  uint8_t tBytes[3]={0};
  if(!readBytes(tBytes,sizeof(tBytes))) return false;
  diag.crcExpected=crc8(tBytes,2);
  diag.crcReceived=tBytes[2];
  if(diag.crcExpected!=diag.crcReceived) {
    diag.error=Error::TemperatureCrc;
    return false;
  }
  // У измерения температуры бит 1 статусного поля должен быть равен 0
  if(tBytes[1] & 0x02) {
    diag.status=tBytes[1];
    diag.error=Error::StatusFlags;
    return false;
  }

  // 2. Запускаем измерение влажности в режиме No-Hold Master (0xF5)
  if(!command8(HTU21_CMD_TRIG_HUM_NOHOLD)) return false;
  uint8_t hBytes[3]={0};
  bool humOk=false;
  uint32_t waitStart=millis();
  // Время преобразования 12-битной влажности у HTU21D составляет 12..16 мс
  while((uint32_t)(millis()-waitStart)<25UL) {
    delay(3);
    if(bus.requestFrom(addr,(size_t)3,true)==3) {
      for(uint8_t i=0;i<3;++i) hBytes[i]=(uint8_t)bus.read();
      diag.expected=3; diag.received=3;
      humOk=true;
      break;
    }
  }
  if(!humOk) {
    diag.expected=3; diag.received=0;
    diag.error=Error::ShortRead;
    return false;
  }

  diag.humCrcExpected=crc8(hBytes,2);
  diag.humCrcReceived=hBytes[2];
  if(diag.humCrcExpected!=diag.humCrcReceived) {
    diag.error=Error::HumidityCrc;
    return false;
  }

  // 3. Пересчёт по формулам даташита HTU21D / SHT21 (маскируем 2 младших статусных бита)
  uint16_t rawT = (((uint16_t)tBytes[0]<<8) | tBytes[1]) & 0xFFFC;
  uint16_t rawH = (((uint16_t)hBytes[0]<<8) | hBytes[1]) & 0xFFFC;

  float t = -46.85f + 175.72f * ((float)rawT / 65536.0f);
  float h = -6.0f   + 125.0f  * ((float)rawH / 65536.0f);

  // Температурная компенсация влажности согласно даташиту HTU21D (-0.15 %RH/°C от 25 °C)
  h += (25.0f - t) * (-0.15f);
  if(h > 100.0f && h <= 108.0f) h = 100.0f;
  if(h < 0.0f   && h >= -6.0f)  h = 0.0f;

  if(!isfinite(t) || !isfinite(h) || t < -40.0f || t > 125.0f || h < 0.0f || h > 100.0f) {
    diag.error=Error::ValueOutOfRange;
    return false;
  }

  temperature = t;
  humidity    = h;
  return true;
}
