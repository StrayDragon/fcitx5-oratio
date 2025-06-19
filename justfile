LIB_NAME:="oratio"

default:
	@just -l

build:
    @mkdir -p build
    @cd build && cmake .. && make -j$(nproc)


install:
    @cd build && sudo make install


uninstall:
    @sudo rm -rf /usr/lib/fcitx5/lib{{LIB_NAME}}.so
    @sudo rm -rf /usr/share/fcitx5/addon/{{LIB_NAME}}.conf
    @sudo rm -rf /usr/share/fcitx5/conf/{{LIB_NAME}}.conf

clean:
    @rm -rf build/