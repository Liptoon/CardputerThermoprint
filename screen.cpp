#include "screen.h"

void screenInit()
{
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.clear();
}

void screenClear()
{
    M5.Display.clear();
    M5.Display.setCursor(0,0);
}

void printLine(const char* text)
{
    M5.Display.println(text);
}