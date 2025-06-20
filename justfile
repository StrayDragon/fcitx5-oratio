LIB_NAME:="oratio"

default:
	@just -l

build:
    @mkdir -p build
    @cd build && cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc)

install:
    @cd build && sudo make install


uninstall:
    @sudo rm -rf /usr/lib/fcitx5/{{LIB_NAME}}.so
    @sudo rm -rf /usr/share/fcitx5/addon/{{LIB_NAME}}.conf
    @sudo rm -rf /usr/share/fcitx5/conf/{{LIB_NAME}}.conf

clean:
    @rm -rf build/