.PHONY: all configure clean run install

all: configure
	meson compile -C build

configure:
	@if [ ! -d build ]; then meson setup build --buildtype=release; fi

run: all
	./build/chronoledger

install: all
	meson install -C build

clean:
	rm -rf build
