R=../..
include ${R}/config.mk

ifeq ($(or $(findstring $(UNAME),SunOS), $(findstring $(UNAME),AIX)),)
LOCAL_LDFLAGS+=-Wl,-soname,libmosquittopp.so.${SOVERSION}
endif
LOCAL_CPPFLAGS+=
LOCAL_CXXFLAGS+=-fPIC
LOCAL_LIBADD+=

.PHONY : clean install

ALL_DEPS=libmosquittopp.so.${SOVERSION}

ifeq ($(WITH_STATIC_LIBRARIES),yes)
    ALL_DEPS+=libmosquittopp.a
endif

all : ${ALL_DEPS}

install : all
	$(INSTALL) -d "${DESTDIR}${libdir}/"
	$(INSTALL) ${STRIP_OPTS} libmosquittopp.so.${SOVERSION} "${DESTDIR}${libdir}/libmosquittopp.so.${SOVERSION}"
	ln -sf libmosquittopp.so.${SOVERSION} "${DESTDIR}${libdir}/libmosquittopp.so"
ifeq ($(WITH_STATIC_LIBRARIES),yes)
	$(INSTALL) libmosquittopp.a "${DESTDIR}${libdir}/libmosquittopp.a"
ifneq ($(UNAME),AIX)
	${CROSS_COMPILE}${STRIP} -g --strip-unneeded "${DESTDIR}${libdir}/libmosquittopp.a"
endif
endif
	$(INSTALL) -d "${DESTDIR}${libdir}/pkgconfig/"
	$(INSTALL) -m644 ${R}/libmosquittopp.pc.in "${DESTDIR}${libdir}/pkgconfig/libmosquittopp.pc"
	sed ${SEDINPLACE} -e "s#@CMAKE_INSTALL_PREFIX@#${prefix}#" -e "s#@VERSION@#${VERSION}#" "${DESTDIR}${libdir}/pkgconfig/libmosquittopp.pc"

uninstall :
	-rm -f "${DESTDIR}${libdir}/libmosquittopp.so.${SOVERSION}"
	-rm -f "${DESTDIR}${libdir}/libmosquittopp.so"
	-rm -f "${DESTDIR}${libdir}/libmosquittopp.a"

clean :
	-rm -f *.o libmosquittopp.so.${SOVERSION} libmosquittopp.a

libmosquittopp.so.${SOVERSION} : mosquittopp.o
	${CROSS_COMPILE}$(CXX) -shared $(LOCAL_LDFLAGS) $< -o $@ ../libmosquitto.so.${SOVERSION} $(LOCAL_LIDADD)

libmosquittopp.a : mosquittopp.o
	${CROSS_COMPILE}$(AR) cr $@ $^

mosquittopp.o : mosquittopp.cpp ${R}/include/mosquitto/libmosquittopp.h
	${CROSS_COMPILE}$(CXX) $(LOCAL_CPPFLAGS) $(LOCAL_CXXFLAGS) -c $< -o $@
