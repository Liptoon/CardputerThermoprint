#include "keyboard.h"

bool keyPressed(char &c)
{
    M5Cardputer.update();

    // Sprawdzamy czy nastąpiła zmiana stanu klawiatury
    if (!M5Cardputer.Keyboard.isChange())
        return false;

    if (M5Cardputer.Keyboard.isPressed())
    {
        auto keys = M5Cardputer.Keyboard.keysState(); 

        // Enter
        if (keys.enter) {
            c = '\n';
            return true;
        }

        // Backspace
        if (keys.del) {
            c = '\b';
            return true;
        }

        // Reszta zwykłych znaków
        if (!keys.word.empty()) {
            c = keys.word[0];
            return true;
        }
    }

    return false;
}