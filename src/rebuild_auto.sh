export OUT_DIR=$1
export PKG_CONFIG_PATH="./build/linux/debian_wheezy_amd64-sysroot/usr/share/pkgconfig"
export CAPNP_INSTALL="/media/data1/zfk/Documents/capnproto-install"
export PATH="$PATH:$CAPNP_INSTALL/bin"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$CAPNP_INSTALL/lib"

#mkdir out/$OUT_DIR
#cp out/args.gn out/$OUT_DIR/args.gn
#gn gen out/$OUT_DIR
#cd out/$OUT_DIR
ninja -C out/$OUT_DIR -t clean
ninja -C out/$OUT_DIR chrome
