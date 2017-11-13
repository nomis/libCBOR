#include "CBOR.h"

// C++ includes
#include <cmath>
#include <cstdint>

// Other includes
#include <Arduino.h>

namespace qindesign {
namespace cbor {

// Major types
constexpr int kUnsignedInt   = 0;
constexpr int kNegativeInt   = 1;
constexpr int kBytes         = 2;
constexpr int kText          = 3;
constexpr int kArray         = 4;
constexpr int kMap           = 5;
constexpr int kTag           = 6;
constexpr int kSimpleOrFloat = 7;

// ***************************************************************************
//  Reader
// ***************************************************************************

DataType Reader::readDataType() {
  // Read the initial byte
  if (state_ == State::kStart) {
    // Initialize everything to a default
    initialByte_ = in_.read();
    value_ = 0;
    syntaxError_ = SyntaxError::kNoError;
    if (initialByte_ < 0) {
      majorType_ = 0;
      addlInfo_ = 0;
      waitAvailable_ = 0;
      return DataType::kEOS;
    }
    majorType_ = static_cast<uint8_t>(initialByte_) >> 5;
    addlInfo_ = initialByte_ & 0x1f;
    state_ = State::kAdditionalInfo;
  }

  // Process the additional info by noting how many bytes we need
  if (state_ == State::kAdditionalInfo) {
    waitAvailable_ = 0;
    switch (addlInfo_) {
      case 24:
        waitAvailable_ = 1;
        state_ = State::kWaitAvailable;
        break;
      case 25:
        waitAvailable_ = 2;
        state_ = State::kWaitAvailable;
        break;
      case 26:
        waitAvailable_ = 4;
        state_ = State::kWaitAvailable;
        break;
      case 27:
        waitAvailable_ = 8;
        state_ = State::kWaitAvailable;
        break;
      case 28:
      case 29:
      case 30:
        syntaxError_ = SyntaxError::kUnknownAdditionalInfo;
        return DataType::kSyntaxError;
      case 31:
        switch (majorType_) {
          case kUnsignedInt:
          case kNegativeInt:
          case kTag:
            syntaxError_ = SyntaxError::kNotAnIndefiniteType;
            return DataType::kSyntaxError;
          case kSimpleOrFloat:  // Floating-point numbers and simple data types
            // Always allow breaks
            break;
        }
        state_ = State::kReadValue;
        break;
      default:
        state_ = State::kReadValue;
    }
  }

  // If we need to, wait for any available bytes
  if (state_ == State::kWaitAvailable) {
    if (in_.available() < waitAvailable_) {
      return DataType::kEOS;
    }
    state_ = State::kReadValue;
  }

  // Read the value from the stream
  if (state_ == State::kReadValue) {
    switch (addlInfo_) {
      case 24:
        value_ = in_.read();
        break;
      case 25:
        value_ =
            (static_cast<uint16_t>(in_.read()) << 8) |
            (static_cast<uint16_t>(in_.read()));
        break;
      case 26:
        value_ =
            (static_cast<uint32_t>(in_.read()) << 24) |
            (static_cast<uint32_t>(in_.read()) << 16) |
            (static_cast<uint32_t>(in_.read()) << 8) |
            (static_cast<uint32_t>(in_.read()));
        break;
      case 27:
        value_ =
            (static_cast<uint64_t>(in_.read()) << 56) |
            (static_cast<uint64_t>(in_.read()) << 48) |
            (static_cast<uint64_t>(in_.read()) << 40) |
            (static_cast<uint64_t>(in_.read()) << 32) |
            (static_cast<uint64_t>(in_.read()) << 24) |
            (static_cast<uint64_t>(in_.read()) << 16) |
            (static_cast<uint64_t>(in_.read()) << 8) |
            (static_cast<uint64_t>(in_.read()));
        break;
      case 28:
      case 29:
      case 30:
        // Shouldn't happen, caught before
        value_ = addlInfo_;
        break;
      case 31:  // Indefinite length or break
        value_ = 0;
        break;
      default:
        value_ = addlInfo_;
    }
    state_ = State::kDetermineType;
  }

  if (state_ == State::kDetermineType) {
    state_ = State::kStart;
    switch (majorType_) {
      case kUnsignedInt:
        return DataType::kUnsignedInt;
      case kNegativeInt:
        return DataType::kNegativeInt;
      case kBytes:
        return DataType::kBytes;
      case kText:
        return DataType::kText;
      case kArray:
        return DataType::kArray;
      case kMap:
        return DataType::kMap;
      case kTag:
        return DataType::kTag;
      case kSimpleOrFloat:  // Floating-point numbers and simple data types
        switch (addlInfo_) {
          case 20:  // False
          case 21:  // True
            value_ = 0;
            return DataType::kBoolean;
          case 22:
            value_ = 0;
            return DataType::kNull;
          case 23:
            value_ = 0;
            return DataType::kUndefined;
          case 24:
            if (value_ < 32) {
              syntaxError_ = SyntaxError::kBadSimpleValue;
              return DataType::kSyntaxError;
            }
            return DataType::kSimpleValue;
            break;
          case 25:
          case 26:
            return DataType::kFloat;
          case 27:
            return DataType::kDouble;
          case 28:
          case 29:
          case 30:
            // Shouldn't happen, caught before
            syntaxError_ = SyntaxError::kUnknownAdditionalInfo;
            return DataType::kSyntaxError;
          case 31:
            value_ = 0;
            return DataType::kBreak;
          default:
            return DataType::kSimpleValue;
        }
        break;
      default:
        // Shouldn't happen
        return DataType::kUnsignedInt;
    }
  }

  return DataType::kEOS;
}

int Reader::readBytes(uint8_t *buffer, size_t length) {
  return in_.readBytes(buffer, length);
}

SyntaxError Reader::getSyntaxError() {
  return syntaxError_;
}

uint64_t getRawValue() {
  return value_;
}

bool Reader::isIndefiniteLength() {
  switch (majorType_) {
    case kBytes:  // Bytes
    case kText:  // Text
    case kArray:  // Array
    case kMap:  // Map
      return addlInfo_ == 31;
  }
  return false;
}

uint64_t Reader::getLength() {
  return value_;
}

bool Reader::getBoolean() {
  if (majorType_ == kSimpleOrFloat) {
    if (addlInfo_ == 21) {
      return true;
    }
    if (addlInfo_ == 24 && value_ == 21) {
      return true;
    }
  }
  return false;
}

float Reader::getFloat() {
  return static_cast<float>(getDouble());
}

double Reader::getDouble() {
  // NOTE: Doing the conversion this way avoids endian and size differences

  if (majorType_ != kSimpleOrFloat) {
    return 0.0;
  }

  if (addlInfo_ == 25) {  // Half-precision
    constexpr int kBitsM = 10;
    constexpr int kBitsE = 5;
    constexpr int kExpBias = (1 << (kBitsE - 1)) - 1;  // 15
    uint16_t half = static_cast<uint16_t>(value_);
    int e = (half >> kBitsM) & ((1 << kBitsE) - 1);
    int m = half & ((1 << kBitsM) - 1);
    double val;
    if (e == 0) {
      val = ldexp(m, 1 - kExpBias - kBitsM);
    } else if (e != (1 << kBitsE) - 1) {
      val = ldexp(m + (1 << kBitsM), e - kExpBias - kBitsM);
    } else {
      val = (m == 0) ? INFINITY : NAN;
    }
    double sign = (half & (1 << (kBitsM + kBitsE))) ? -1 : 1;
    return copysign(val, sign);
  }

  if (addlInfo_ == 26) {  // Single-precision
    float f;
    uint32_t bits = static_cast<uint32_t>(value_);
    memcpy(&f, &bits, 4);  // TODO: Is the size always 4?
    return f;
    // constexpr int kBitsM = 23;
    // constexpr int kBitsE = 8;
    // constexpr int kExpBias = (1 << (kBitsE - 1)) - 1;  // 127
    // uint32_t single = static_cast<uint32_t>(value_);
    // int e = (single >> kBitsM) & ((1 << kBitsE) - 1);
    // long m = single & ((1L << kBitsM) - 1);
    // double val;
    // if (e == 0) {
    //   val = ldexp(m, 1 - kExpBias - kBitsM);
    // } else if (e != (1 << kBitsE) - 1) {
    //   val = ldexp(m + (1L << kBitsM), e - kExpBias - kBitsM);
    // } else if (m == 0) {
    //   val = INFINITY;
    // } else {  // NaN
    //   float f;
    //   uint32_t bits = static_cast<uint32_t>(value_);
    //   memcpy(&f, &bits, 4);  // TODO: Is the size always 4?
    //   return f;
    // }
    // double sign = (single & (1L << (kBitsM + kBitsE))) ? -1 : 1;
    // return copysign(val, sign);
  }

  if (addlInfo_ == 27) {  // Double-precision
    double val;
    memcpy(&val, &value_, 8);  // TODO: Is the size always 8?
    return val;
    // constexpr int kBitsM = 52;
    // constexpr int kBitsE = 11;
    // constexpr int kExpBias = (1 << (kBitsE - 1)) - 1;  // 1023
    // int e = (value_ >> kBitsM) & ((1 << kBitsE) - 1);
    // long long m = value_ & ((1LL << kBitsM) - 1);
    // double val;
    // if (e == 0) {
    //   val = ldexp(m, 1 - kExpBias - kBitsM);
    // } else if (e != (1 << kBitsE) - 1) {
    //   val = ldexp(m + (1LL << kBitsM), e - kExpBias - kBitsM);
    // } else if (m == 0) {
    //   val = INFINITY;
    // } else {  // NaN
    //   memcpy(&val, &value_, 8);  // TODO: Is the size always 8?
    //   return val;
    // }
    // double sign = (value_ & (1LL << (kBitsM + kBitsE))) ? -1 : 1;
    // return copysign(val, sign);
  }

  return 0.0;
}

uint64_t Reader::getUnsignedInt() {
  if (majorType_ == kUnsignedInt) {
    return value_;
  }
  return 0ULL;
}

int64_t Reader::getInt() {
  if (majorType_ == kNegativeInt) {
    return -1LL - static_cast<int64_t>(value_);
  }
  return 0LL;
}

uint8_t Reader::getSimpleValue() {
  if (majorType_ == kSimpleOrFloat) {
    return static_cast<uint8_t>(value_);
  }
  return 0;
}

uint64_t Reader::getTag() {
  if (majorType_ == kTag) {
    return value_;
  }
  return 0ULL;
}

// ***************************************************************************
//  Well-formedness checks
// ***************************************************************************

bool Reader::isWellFormed() {
  bool retval = (isWellFormed(false) >= 0);
#ifdef ESP8266
  yield();
#endif
  return retval;
}

int Reader::isWellFormed(bool breakable) {
  int ib = in_.read();  // Initial byte
  if (ib < 0) {
    return -1;
  }
  uint8_t majorType = static_cast<uint8_t>(ib) >> 5;
  uint8_t ai = ib & 0x1f;  // Additional information
  uint64_t val;
  switch (ai) {
    case 24: {
      int16_t v = in_.read();
      if (v < 0) {
        return -1;
      }
      val = static_cast<uint8_t>(v);
      if (majorType == kSimpleOrFloat && val < 32) {
        return -1;
      }
      break;
    }
    case 25: {
      int32_t v = (int32_t{in_.read()} << 8) | int32_t{in_.read()};
      if (v < 0) {
        return -1;
      }
      val = static_cast<uint16_t>(v);
      break;
    }
    case 26: {
      int64_t v =
          (int64_t{in_.read()} << 24) |
          (int64_t{in_.read()} << 16) |
          (int64_t{in_.read()} << 8) |
          (int64_t{in_.read()});
      if (v < 0) {
        return -1;
      }
      val = static_cast<uint32_t>(v);
      break;
    }
    case 27:
      val = 0;
      for (int i = 0; i < 2; i++) {
        int64_t v =
            (int64_t{in_.read()} << 24) |
            (int64_t{in_.read()} << 16) |
            (int64_t{in_.read()} << 8) |
            (int64_t{in_.read()});
        if (v < 0) {
          return -1;
        }
        val = (val << 32) | static_cast<uint32_t>(v);
      }
      break;
    case 28: case 29: case 30:
      return -1;
    case 31:
      return isIndefiniteWellFormed(majorType, breakable);
    default:
      val = ai;
  }

  switch (majorType) {
    // No content for 0, 1, or 7
    case 2:  // Byte string
    case 3:  // Text string (UTF-8)
      if (val <= UINT32_MAX) {
        for (uint32_t i = 0, max = static_cast<uint32_t>(val); i < max; i++) {
          if (in_.read() < 0) {
            return -1;
          }
        }
      } else {
        for (uint64_t i = 0; i < val; i++) {
          if (in_.read() < 0) {
            return -1;
          }
        }
      }
      break;
    case 5:  // Map
      // Check for overflow
      if (val != 0 && 2*val <= val) {
        return -1;
      }
      val <<= 1;
      // fallthrough
    case 4:  // Array
      if (val <= UINT32_MAX) {
        for (uint32_t i = 0, max = static_cast<uint32_t>(val); i < max; i++) {
          if (isWellFormed(false) < 0) {
            return -1;
          }
        }
      } else {
        for (uint64_t i = 0; i < val; i++) {
          if (isWellFormed(false) < 0) {
            return -1;
          }
        }
      }
      break;
    case 6:
      if (isWellFormed(false) < 0) {
        return -1;
      }
      break;
    default:
      // Unsigned integer (0), Negative integer (1),
      // Floating-point numbers and simple data types (7)
      break;
  }
  return majorType;
}

int Reader::isIndefiniteWellFormed(uint8_t majorType, bool breakable) {
  switch (majorType) {
    case kBytes:
    case kText:
      while (true) {
        int t = isWellFormed(true);
        if (t == -2) {  // Break
          break;
        }
        if (t == -1) {  // Malformed
          return -1;
        }
        if (t != majorType) {
          return -1;
        }
      }
      break;
    case kArray:
      while (true) {
        int t = isWellFormed(true);
        if (t == -2) {  // Break
          break;
        }
        if (t == -1) {  // Malformed
          return -1;
        }
      }
      break;
    case kMap:
      while (true) {
        int t = isWellFormed(true);
        if (t == -2) {  // Break
          break;
        }
        if (t == -1) {  // Malformed
          return -1;
        }
        if (isWellFormed(false) < 0) {
          return -1;
        }
      }
      break;
    case kSimpleOrFloat:  // Floating-point numbers and simple data types
      if (breakable) {
        return -2;
      }
      return -1;
    default:
      // Unsigned integer (0), Negative integer (1), Tag (6)
      return -1;
  }

  return majorType;
}

// ***************************************************************************
//  Writer
// ***************************************************************************

void Writer::writeBoolean(bool b) {
  out_.write((kSimpleOrFloat << 5) + (b ? 21 : 20));
}

void Writer::writeFloat(float f) {
  out_.write((kSimpleOrFloat << 5) + 26);

  // constexpr int kBitsM = 23;
  // constexpr int kBitsE = 8;

  uint32_t val;
  memcpy(&val, &f, 4);  // TODO: Is the size always 4?
  // if (std::isnan(f)) {
  //   memcpy(&val, &f, 4);  // TODO: Is the size always 4?
  // } else if (std::isinf(f)) {
  //   // All 1's for the exponent
  //   val = ((1L << kBitsE) - 1L) << kBitsM;
  // } else if (f == 0) {
  //   val = 0;
  // } else {
  //   float f2 = (f < 0) ? -f : f;
  //   constexpr int kExpBias = (1 << (kBitsE - 1)) - 1;  // 127
  //   int e = ilogb(f2);
  //   long m;
  //   if (e <= -kExpBias) {
  //     // exp = 1 - kExpBias - kBitsM
  //     e = 0;
  //     m = static_cast<long>(scalbn(f2, -(1 - kExpBias - kBitsM)));
  //   } else {
  //     // exp = e - kExpBias - kBitsM
  //     m = static_cast<long>(scalbn(f2, -(e - kBitsM))) - (1L << kBitsM);
  //     e += kExpBias;
  //   }
  //   val = (long{e} << kBitsM) | m;
  // }
  // if (!std::isnan(f) && std::signbit(f)) {
  //   val |= (1L << 31);
  // }

  out_.write(val >> 24);
  out_.write(val >> 16);
  out_.write(val >> 8);
  out_.write(val);
}

void Writer::writeDouble(double d) {
  out_.write((kSimpleOrFloat << 5) + 27);

  // constexpr int kBitsM = 52;
  // constexpr int kBitsE = 11;

  uint64_t val;
  memcpy(&val, &d, 8);  // TODO: Is the size always 8?
  // if (std::isnan(d)) {
  //   memcpy(&val, &d, 8);  // TODO: Is the size always 8?
  // } else if (std::isinf(d)) {
  //   // All 1's for the exponent
  //   val = ((1LL << kBitsE) - 1LL) << kBitsM;
  // } else if (d == 0) {
  //   val = 0;
  // } else {
  //   double d2 = (d < 0) ? -d : d;
  //   constexpr int kExpBias = (1 << (kBitsE - 1)) - 1;  // 1023
  //   int e = ilogb(d2);
  //   long long m;
  //   if (e <= -kExpBias) {
  //     // exp = 1 - kExpBias - kBitsM
  //     e = 0;
  //     m = static_cast<long long>(scalbn(d2, -(1 - kExpBias - kBitsM)));
  //   } else {
  //     // exp = e - kExpBias - kBitsM
  //     m = static_cast<long long>(scalbn(d2, -(e - kBitsM))) - (1LL << kBitsM);
  //     e += kExpBias;
  //   }
  //   val = ((long long){e} << kBitsM) | m;
  // }
  // if (!std::isnan(d) && std::signbit(d)) {
  //   val |= (1LL << 63);
  // }

  out_.write(val >> 56);
  out_.write(val >> 48);
  out_.write(val >> 40);
  out_.write(val >> 32);
  out_.write(val >> 24);
  out_.write(val >> 16);
  out_.write(val >> 8);
  out_.write(val);
}

void Writer::writeUnsignedInt(uint64_t u) {
  writeTypedInt(kUnsignedInt << 5, u);
}

void Writer::writeInt(int64_t i) {
  uint64_t u = i >> 63;                  // Extend sign
  uint8_t mt = u & (kNegativeInt << 5);  // Major type, 0x20 (signed) or 0x00 (unsigned)
  u ^= i;                                // Complement negatives, equivalent to -1 - i
  writeTypedInt(mt, u);
}

void Writer::writeTypedInt(uint8_t mt, uint64_t u) {
  if (u < 24) {
    out_.write(mt + u);
  } else if (u < (1 << 8)) {
    out_.write(mt + 24);
    out_.write(u);
  } else if (u < (1L << 16)) {
    out_.write(mt + 25);
    out_.write(u >> 8);
    out_.write(u);
  } else if (u < (1LL << 32)) {
    out_.write(mt + 26);
    out_.write(u >> 24);
    out_.write(u >> 16);
    out_.write(u >> 8);
    out_.write(u);
  } else {
    out_.write(mt + 27);
    out_.write(u >> 56);
    out_.write(u >> 48);
    out_.write(u >> 40);
    out_.write(u >> 32);
    out_.write(u >> 24);
    out_.write(u >> 16);
    out_.write(u >> 8);
    out_.write(u);
  }
}

void Writer::writeNull() {
  out_.write((kSimpleOrFloat << 5) + 22);
}

void Writer::writeUndefined() {
  out_.write((kSimpleOrFloat << 5) + 23);
}

void Writer::writeSimpleValue(uint8_t v) {
  if (v < 24) {
    out_.write((kSimpleOrFloat << 5) + v);
  } else {
    out_.write((kSimpleOrFloat << 5) + 24);
    out_.write(v);
  }
}

void Writer::writeTag(uint64_t v) {
  writeTypedInt(kTag << 5, v);
}

void Writer::writeBytes(uint8_t *buffer, size_t length) {
  out_.write(buffer, length);
}

void Writer::beginBytes(unsigned int length) {
  writeTypedInt(kBytes << 5, length);
}

void Writer::beginText(unsigned int length) {
  writeTypedInt(kText << 5, length);
}

void Writer::beginIndefiniteBytes() {
  out_.write((kBytes << 5) + 31);
}

void Writer::beginIndefiniteText() {
  out_.write((kText << 5) + 31);
}

void Writer::beginArray(unsigned int length) {
  writeTypedInt(kArray << 5, length);
}

void Writer::beginMap(unsigned int length) {
  writeTypedInt(kMap << 5, length);
}

void Writer::beginIndefiniteArray() {
  out_.write((kArray << 5) + 31);
}

void Writer::beginIndefiniteMap() {
  out_.write((kMap << 5) + 31);
}

void Writer::endIndefinite() {
  out_.write((kSimpleOrFloat << 5) + 31);
}

// // Forward declarations
// int isWellFormed(int *address, bool breakable);
// int isIndefiniteWellFormed(int address, uint8_t majorType, bool breakable);
//
// bool isEEPROMWellFormed(int address) {
//   return isWellFormed(false, &address) >= 0;
// }
//
// // Checks if an item is well-formed and returns its major type. address
// // is modified to be just past the item, or -1 if the item is not valid.
// int isWellFormed(int *address, bool breakable) {
//   uint8_t ib = EEPROM.read((*address)++);  // Initial byte
//   uint8_t majorType = ib >> 5;  // Major type
//   uint64_t val;
//   switch (ib & 0x1f) {  // Additional information
//     case 24:
//       val = EEPROM.read((*address)++);
//       break;
//     case 25:
//       val =
//           (uint16_t{EEPROM.read(*address + 0)} << 8) |
//           (uint16_t{EEPROM.read(*address + 1)});
//       *address += 2;
//       break;
//     case 26:
//       val =
//           (uint32_t{EEPROM.read(*address + 0)} << 24) |
//           (uint32_t{EEPROM.read(*address + 1)} << 16) |
//           (uint32_t{EEPROM.read(*address + 2)} << 8) |
//           (uint32_t{EEPROM.read(*address + 3)});
//       *address += 4;
//       break;
//     case 27:
//       val =
//           (uint64_t{EEPROM.read(*address + 0)} << 56) |
//           (uint64_t{EEPROM.read(*address + 1)} << 48) |
//           (uint64_t{EEPROM.read(*address + 2)} << 40) |
//           (uint64_t{EEPROM.read(*address + 3)} << 32) |
//           (uint64_t{EEPROM.read(*address + 4)} << 24) |
//           (uint64_t{EEPROM.read(*address + 5)} << 16) |
//           (uint64_t{EEPROM.read(*address + 6)} << 8) |
//           (uint64_t{EEPROM.read(*address + 7)});
//       *address += 4;
//       break;
//     case 28: case 29: case 30:
//       return -1;
//     case 31:
//       return isIndefiniteWellFormed(address, majorType, breakable);
//     default:
//       return -1;
//   }
//
//   switch (majorType) {  // Major type
//     // No content for 0, 1, or 7
//     case 0:  // Unsigned integer
//     case 1:  // Negative integer
//     case 7:  // Floating-point numbers and simple data types
//       break;
//     case 2:  // Byte string
//     case 3:  // Text string (UTF-8)
//       *address += val;
//       break;
//     case 5:  // Map
//       // Check for overflow
//       if (2*val <= val) {
//         return -1;
//       }
//       val <<= 1;
//       // fallthrough
//     case 4:  // Array
//       if (val <= 0xffffffff) {
//         for (uint32_t i = 0, max = static_cast<uint32_t>(val); i < max; i++) {
//           if (isWellFormed(address, breakable) < 0) {
//             return -1;
//           }
//         }
//       } else {
//         for (uint64_t i = 0; i < val; i++) {
//           if (isWellFormed(address, breakable) < 0) {
//             return -1;
//           }
//         }
//       }
//       break;
//     case 6:
//       if (isWellFormed(address, breakable) < 0) {
//         return -1;
//       }
//       break;
//     default:
//       return -1;
//   }
//   return address;
// }
//
// int isIndefiniteWellFormed(int *address, uint8_t majorType, bool breakable) {
//   switch (majorType) {
//     case 2:  // Byte string
//     case 3:  // Text string (UTF-8)
//       while (true) {
//         int itemType = isWellFormed(address, true);
//         if (itemType != majorType) {
//           return -1;
//         }
//       }
//       break;
//     case 4:  // Array
//
//     case 5:  // Map
//     case 7:  // Floating-point numbers and simple data types
//       if (breakable) {
//         return -2;
//       }
//       return -1;
//   }
//   return address;
// }

}  // namespace cbor
}  // namespace qindesign
