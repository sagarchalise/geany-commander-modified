geany-commander-modified
======================

Modification to **geany-commander** to browser symbols.

Currently lists symbol and goto functionality on line.

####TODO
 - Jump to symbols on other doc with @ and #
 - Project file browsing and symbol jump functionality

  **inspired from sublime goto functionality
 

$ gcc -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"commander-modified\" -c commander-modified.c -fPIC `pkg-config --cflags geany`

$ gcc commander-modified.o -o commander-modified.so -shared `pkg-config --libs geany`


**Copy commander-modified.so to plugin path**
