all: build
	
build:	
	gcc -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"geany-goto\" -c ./commander-modified.c -fPIC `pkg-config --cflags geany`
	gcc commander-modified.o -o geany-goto.so -shared `pkg-config --libs geany`

install: uninstall startinstall

startinstall:
	cp -f ./geany-goto.so ~/.config/geany/plugins
	chmod 755 ~/.config/geany/plugins/geany-goto.so

uninstall:
	rm -f ~/.config/geany/plugins/geany-goto*

clean:
	rm -f ./geany-goto.so
	rm -f ./geany-goto.o
