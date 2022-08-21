#include "converter.h"

String digitsToString(int i)
{
  char ch[3];
  sprintf(ch, "%.2d", i);
  String str = String(ch);
  return str;
}

String timeToString(time_t time)
{
  char ch[9];
  sprintf(ch, "%.2d:%.2d:%.2d",
          hour(time), minute(time), second(time));
  String str = String(ch);
  return str;
}

String timeToString(time_t time, int16_t millis)
{
  if (millis > 999)
  {             
    millis = 999; //бывает 1000
  }
  char ch[13];
  sprintf(ch, "%.2d:%.2d:%.2d,%.3d",
          hour(time), minute(time), second(time), millis);
  String str = String(ch);
  return str;
}