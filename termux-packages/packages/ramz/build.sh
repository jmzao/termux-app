TERMUX_PKG_HOMEPAGE=https://github.com/jmzao/Termux-ZRAM
TERMUX_PKG_DESCRIPTION=Opt-in\ userspace\ compressed\ block\ store\ for\ Termux-ZRAM
TERMUX_PKG_LICENSE=MIT
TERMUX_PKG_MAINTAINER=@jmzao
TERMUX_PKG_VERSION=0.1.0
TERMUX_PKG_SRCURL=https://github.com/jmzao/termux-app/archive/refs/heads/zram-runtime.tar.gz
TERMUX_PKG_SHA256=REPLACE_WITH_WORKFLOW_SHA256
TERMUX_PKG_DEPENDS=liblz4,\ zstd
TERMUX_PKG_BUILD_DEPENDS=liblz4,\ zstd
TERMUX_PKG_AUTO_UPDATE=false
TERMUX_PKG_BUILD_IN_SRC=true
termux_step_configure() { :; }
termux_step_make() { cd ${TERMUX_PKG_SRCDIR}/ramz-native; make build/libramz.so build/ramz build/ramz-benchmark CXXFLAGS=${CXXFLAGS}\ ${CPPFLAGS:-} LZ4_LIB=-l:liblz4.so.1 ZSTD_LIB=-l:libzstd.so.1; }
termux_step_make_install() { cd ${TERMUX_PKG_SRCDIR}/ramz-native; make PREFIX=${TERMUX_PREFIX} DESTDIR=${TERMUX_PKG_MASSAGEDIR} CXXFLAGS=${CXXFLAGS}\ ${CPPFLAGS:-} LZ4_LIB=-l:liblz4.so.1 ZSTD_LIB=-l:libzstd.so.1 install; }
