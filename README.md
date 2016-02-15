geany-commander-modified
======================

Modification to **geany-commander** to browser symbols and jump to open documents only

####TODO
 - Jump to symbols on other doc with @ and #
 - Project file browsing and symbol jump functionality


$ gcc -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"commander-modified\" -c commander-modified.c -fPIC `pkg-config --cflags geany`

$ gcc commander-modified.o -o commander-modified.so -shared `pkg-config --libs geany`


**Copy geany-symbol.so to plugin path**
