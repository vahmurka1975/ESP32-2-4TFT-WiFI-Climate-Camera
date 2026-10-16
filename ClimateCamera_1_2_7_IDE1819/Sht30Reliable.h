#ifndef SHT30_RELIABLE_H
#define SHT30_RELIABLE_H
#include <Arduino.h>
#include <Wire.h>

// Надёжный драйвер датчика GY-21 (HTU21D / SHT21) по шине I2C (адрес 0x40, SDA=21, SCL=22).
// Сохраняет имя класса Sht30Reliable для полной совместимости с архитектурой прошивки.
class Sht30Reliable {
 public:
  enum class Error : uint8_t {
    None, I2cBegin, ClockLow, DataLow, WriteCommand, EndTransmission,
    ShortRead, TemperatureCrc, HumidityCrc, BothCrc, StatusCrc,
    StatusFlags, ValueOutOfRange
  };
  struct Diagnostic {
    Error error=Error::None;
    uint8_t wireCode=0, received=0, expected=0;
    uint8_t crcReceived=0, crcExpected=0;
    uint8_t humCrcReceived=0, humCrcExpected=0;
    uint16_t command=0, status=0;
  };
  Sht30Reliable(TwoWire &wire,uint8_t sda,uint8_t scl,uint8_t address=0x40);
  bool begin();
  bool recover();
  bool startMeasurement();
  bool readMeasurement(float &temperature,float &humidity);
  const Diagnostic &diagnostic() const { return diag; }
  const char *errorName() const;
  void formatError(char *out,size_t length) const;
  static uint8_t crc8(const uint8_t *data,size_t length);
 private:
  TwoWire &bus;
  uint8_t pinSda,pinScl,addr;
  Diagnostic diag;
  bool command8(uint8_t value);
  bool writeUserReg(uint8_t value);
  bool readBytes(uint8_t *data,size_t length);
  bool waitClockHigh();
};
#endif
