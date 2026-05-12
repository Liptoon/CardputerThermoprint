#include "splash.h"

void showSplash() {
    const char* messages[] = {"Despicable Me:/>", "Despicable Me:/>_"}; // tablica klatek animacji
    const int msgCount = 2; //dlugosc tablicy klatek animacji

    M5.Display.clear();
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int x = 20, y = 50; 

    for (int i = 0; i < 8; i++) { 
        M5.Display.setCursor(x, y);
        M5.Display.println(messages[i % msgCount]);
        delay(500); 
        M5.Display.fillRect(x, y, 220, 40, TFT_BLACK);
    }
    M5.Display.clear();
    M5.Display.setCursor(0, 0);
}