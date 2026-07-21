#include <stdio.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
//-------------------------------------------------------------------------------------------
enum LedPattern {
    PATTERN_OFF = 0,
    PATTERN_BREATH,
    PATTERN_RAINBOW,
    PATTERN_STROBE,
    PATTERN_CHASE,
    PATTERN_SPARKLE,
    PATTERN_FILL
};
//-------------------------------------------------------------------------------------------
struct LedState {
    LedPattern pattern;
    uint8_t level;
    uint32_t color;
    uint8_t speed;
    uint8_t repeat;
};
//-------------------------------------------------------------------------------------------
enum ConsoleLedMode {
    CONSOLE_LED_MODE_NORMAL = 0,
    CONSOLE_LED_MODE_MONITOR
};
//-------------------------------------------------------------------------------------------
struct DaliLampVisualState {
    uint8_t level;
    uint8_t last_active_level;
};
//-------------------------------------------------------------------------------------------
typedef struct hsl {
    int h; 
    int s;
    int l;
 } HSL;
//-------------------------------------------------------------------------------------------
typedef struct rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
 } RGB;
//-------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------
/// @brief GRB 
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
    ((uint32_t) (g) << 16) |
    ((uint32_t) (r) << 8) |
    (uint32_t) (b);
 }
 //-------------------------------------------------------------------------------------------
 /// @brief GRB 
static inline uint32_t urgb_u32(RGB rgb) {
    return
    ((uint32_t) (rgb.g) << 16) |
    ((uint32_t) (rgb.r) << 8) |
    (uint32_t) (rgb.b);
 }
//-------------------------------------------------------------------------------------------
static RGB u32_RGB(uint32_t u){
    RGB result;
    result.b = u & 0xff;
    result.r = (u >> 8) & 0xff;
    result.g = (u >> 16) & 0xff;
    return result;
 }
 //-------------------------------------------------------------------------------------------
static uint32_t hsl2rgb360(float hue, uint8_t saturation, uint8_t value) {
  uint8_t hueScaled = (uint8_t)(hue / 360.0 * 255.0);
  uint8_t r, g, b;
  float h = hueScaled / 255.0;
  float s = saturation / 255.0;
  float v = value / 255.0;
  float f = h * 6.0 - floor(h * 6.0);
  float p = v * (1.0 - s);
  float q = v * (1.0 - f * s);
  float t = v * (1.0 - (1.0 - f) * s);
  switch ((int)(h * 6.0)) {
    case 0: r = v * 255.0; g = t * 255.0; b = p * 255.0; break;
    case 1: r = q * 255.0; g = v * 255.0; b = p * 255.0; break;
    case 2: r = p * 255.0; g = v * 255.0; b = t * 255.0; break;
    case 3: r = p * 255.0; g = q * 255.0; b = v * 255.0; break;
    case 4: r = t * 255.0; g = p * 255.0; b = v * 255.0; break;
    case 5: r = v * 255.0; g = p * 255.0; b = q * 255.0; break;
  }
  return urgb_u32(r,g,b);
 }
 //------------------------------------------------------------------------------------------- 
static uint32_t rgbBrightness(uint32_t color,uint8_t bright){
    RGB rgb = u32_RGB(color);
    rgb.r = rgb.r * bright / 100;
    rgb.g = rgb.g * bright / 100;
    rgb.b = rgb.b * bright / 100;
    return urgb_u32(rgb);
 }
//-------------------------------------------------------------------------------------------
static std::vector<uint8_t> intToBytes(int32_t paramInt)
{
     std::vector<uint8_t> arrayOfByte(4);
     for (int i = 0; i < 4; i++)
         arrayOfByte[3 - i] = (paramInt >> (i * 8));
     return arrayOfByte;
}
//-------------------------------------------------------------------------------------------
static uint32_t HexToInt(const std::string &data){
    uint32_t hexNumber;
    sscanf(data.c_str(), "%x", &hexNumber);
    return hexNumber;
 }
 //------------------------------------------------------------------------------------------- 
template< typename T >
static std::string IntToHex(T i, int width)
{
    std::stringstream stream;
    if((int)i < 0){
        stream << std::setfill ('0') << std::setw(width) << 0;
        return stream.str();
    }
  stream << std::setfill ('0') << std::setw(width) << std::hex << i;
  return stream.str();
}
//-------------------------------------------------------------------------------------------
/// @brief rescale a number to a diiferent range
static inline uint8_t ReScale(int value,int min,int max,int nmin,int nmax){
    //NewValue = (((OldValue - OldMin) * (NewMax - NewMin)) / (OldMax - OldMin)) + NewMin
    return (((value - min)*(nmax - nmin))/(max - min)) + nmin;
 } 
//-------------------------------------------------------------------------------------------
