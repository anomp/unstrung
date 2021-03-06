#!/bin/sh

set -e
mkdir -p $HOME/stuff
BUILDTOP=$(cd $HOME/stuff; pwd)

if [ ! -x $HOME/stuff/host/tcpdump-4.8.0/tcpdump ]
then
    (cd ${BUILDTOP} && rm -rf libpcap && git clone https://github.com/mcr/libpcap.git )
    (cd ${BUILDTOP} && rm -rf tcpdump && git clone https://github.com/mcr/tcpdump.git )
    (cd ${BUILDTOP} && mkdir -p host/libpcap-1.8.0 && cd host/libpcap-1.8.0 && ../../libpcap/configure --prefix=${BUILDTOP} && make && make install)
    (cd ${BUILDTOP} && mkdir -p host/tcpdump-4.8.0 && cd host/tcpdump-4.8.0 && ../../tcpdump/configure --prefix=${BUILDTOP} && make && make install)
    (cd ${BUILDTOP} && mkdir -p x86/libpcap-1.8.0 && cd x86/libpcap-1.8.0 && CFLAGS=-m32 ../../libpcap/configure --target=i686-pc-linux-gnu && make LDFLAGS=-m32 CFLAGS=-m32)
    (cd ${BUILDTOP} && ln -s x86/libpcap-1.8.0 libpcap && mkdir -p x86/tcpdump-4.8.0 && cd x86/tcpdump-4.8.0 && CFLAGS=-m32 ../../tcpdump/configure --target=i686-pc-linux-gnu && make LDFLAGS=-m32 CFLAGS=-m32)
fi

echo LIBPCAP=${BUILDTOP}/x86/libpcap-1.8.0/libpcap.a >Makefile.local
echo LIBPCAP_HOST=${BUILDTOP}/host/libpcap-1.8.0/libpcap.a >>Makefile.local
echo LIBPCAPINC=-I${BUILDTOP}/include                >>Makefile.local
echo ARCH=i386  			             >>Makefile.local
echo TCPDUMP=${BUILDTOP}/host/tcpdump-4.8.0/tcpdump  >>Makefile.local
echo NETDISSECTLIB=${BUILDTOP}/host/tcpdump-4.8.0/libnetdissect.a >>Makefile.local
echo NETDISSECTH=-DNETDISSECT -I${BUILDTOP}/include -I${BUILDTOP}/host/tcpdump-4.8.0/ -I${BUILDTOP}/tcpdump >>Makefile.local
echo export ARCH LIBPCAP LIBPCAP_HOST LIBPCAPINC TCPDUMP NETDISSECTLIB NETDISSECTH >>Makefile.local
