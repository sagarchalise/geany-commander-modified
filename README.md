geany-goto
======================

Modification to **geany-commander** to browse symbols in current doc.

Currently lists symbol and goto functionality on line with highlighting of symbols.

####TODO
 - Jump to symbols on other doc.
 - Project symbol search and jump functionality

  *inspired from sublime goto functionality*
 

$ gcc -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"geany-goto\" -c commander-modified.c -fPIC `pkg-config --cflags geany`

$ gcc commander-modified.o -o goto.so -shared `pkg-config --libs geany`


**Copy commander-modified.so to plugin path**
