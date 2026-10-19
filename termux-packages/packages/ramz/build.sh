# Copy this package into a com.termuxzram-configured termux-packages tree.
# It installs only an opt-in compressed block store. It never hooks malloc,
# changes Android's kernel zram, or reads memory from another process.
TERMUX_PKG_HOMEPAGE=https://github.com/jmzao/Termux-ZRAM
TERMUX_PKG_DESCRIPTION="Opt-in userspace compressed block store for Termux-ZRAM"
TERMUX_PKG_LICENSE="MIT"
TERMUX_PKG_MAINTAINER="@jmzao"
TERMUX_PKG_VERSION=0.1.0
TERMUX_PKG_SRCURL=https://github.com/jmzao/termux-app/archive/refs/heads/zram-runtime.tar.gz
# The release workflow replaces this with the SHA-256 of the checked-out source.
TERMUX_PKG_SHA256=REPLACE_WITH_WORKFLOW_SHA256
TERMUX_PKG_DEPENDS="liblz4, zstd"
TERMUX_PKG_BUILD_DEPENDS="liblz4, zstd"
TERMUX_PKG_AUTO_UPDATE=false

termux_step_make() {
    cd "$TERMUX_PKG_SRCDIR/ramz-native"
    make LZ4_LIB=-llz4 ZSTD_LIB=-lzstd
}

termux_step_make_install() {
    cd "$TERMUX_PKG_SRCDIR/ramz-native"
    make PREFIX="$TERMUX_PREFIX" DESTDIR="$TERMUX_PKG_MASSAGEDIR" LZ4_LIB=-llz4 ZSTD_LIB=-lzstd install
}
