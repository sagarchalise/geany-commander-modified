geany-symbol
============

Alternative symbol browser for geany with dialog window.

This is a symbol browser for geany where a dialog window opens with all symbols of current file
listed. It highlights all occurence when traversing as well moves to tag definition. The code base is
mostly  **Commander** plugin available on geany-plugins.


$ gcc -c geany-symbol.c -fPIC `pkg-config --cflags geany` 

$ gcc geany-symbol.o -o geany-symbol.so -shared `pkg-config --libs geany` 


**Copy geany-symbol.so to plugin path**