geany-symbol
============

Alternative symbol browser for geany.

***Features***

* Keybinding to open window
* traversing jumps to symbols
* seacrh works on 2 or more characters only

The code base is mostly  **Commander** plugin available on geany-plugins.


$ gcc -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"geany-symbol\" -c geany-symbol.c -fPIC `pkg-config --cflags geany`

$ gcc geany-symbol.o -o geany-symbol.so -shared `pkg-config --libs geany`


**Copy geany-symbol.so to plugin path**
