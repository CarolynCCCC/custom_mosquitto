option(WITH_LIB_CPP "Build C++ library?" ON)
if(WITH_LIB_CPP)
	add_subdirectory(cpp)
endif()

set(C_SRC
	actions_publish.c
	actions_subscribe.c
	actions_unsubscribe.c
	alias_mosq.c alias_mosq.h
	callbacks.c
	connect.c
	extended_auth.c
	handle_auth.c
	handle_connack.c
	handle_disconnect.c
	handle_ping.c
	handle_pubackcomp.c
	handle_publish.c
	handle_pubrec.c
	handle_pubrel.c
	handle_suback.c
	handle_unsuback.c
	helpers.c
	http_client.c http_client.h
	logging_mosq.c logging_mosq.h
	loop.c
	messages_mosq.c messages_mosq.h
	mosquitto.c ../include/mosquitto.h
	mosquitto_internal.h
	../include/mosquitto/mqtt_protocol.h
	net_mosq_ocsp.c net_mosq.c net_mosq.h
	net_ws.c
	options.c
	packet_datatypes.c
	packet_mosq.c packet_mosq.h
	property_mosq.c property_mosq.h
	read_handle.c read_handle.h
	send_connect.c
	send_disconnect.c
	send_mosq.c
	send_publish.c
	send_subscribe.c
	send_unsubscribe.c
	send_mosq.c send_mosq.h
	socks_mosq.c
	srv_mosq.c
	thread_mosq.c
	tls_mosq.c
	util_mosq.c util_mosq.h
	will_mosq.c will_mosq.h)

set(LIBRARIES common-options ${OPENSSL_LIBRARIES})

if(WITH_TLS)
	set (LIBRARIES ${LIBRARIES} OpenSSL::SSL)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
	find_library(LIBRT rt)
	if(LIBRT)
		set (LIBRARIES ${LIBRARIES} rt)
	endif()
endif()

if(WIN32)
	set (LIBRARIES ${LIBRARIES} ws2_32)
endif()

if(WITH_SRV)
	# Simple detect c-ares
	find_path(ARES_HEADER ares.h)
	if(ARES_HEADER)
		add_definitions("-DWITH_SRV")
		set (LIBRARIES ${LIBRARIES} cares)
	else()
		message(WARNING "c-ares library not found.")
	endif()
endif()

if(WITH_WEBSOCKETS AND WITH_WEBSOCKETS_BUILTIN)
	add_definitions("-DWITH_WEBSOCKETS=WS_IS_BUILTIN")
	set(C_SRC ${C_SRC}
		../deps/picohttpparser/picohttpparser.c
		http_client.c http_client.h
		net_ws.c
		)
endif()

add_library(libmosquitto SHARED
	${C_SRC}
)

if(WITH_WEBSOCKETS AND WITH_WEBSOCKETS_BUILTIN)
	target_include_directories(libmosquitto PRIVATE
		"${mosquitto_SOURCE_DIR}/deps/picohttpparser")
endif()

target_include_directories(libmosquitto
	PUBLIC
		"${mosquitto_SOURCE_DIR}/include"
		"${mosquitto_SOURCE_DIR}/lib"
	PRIVATE
		"${mosquitto_SOURCE_DIR}"
		"${mosquitto_SOURCE_DIR}/common"
		"${mosquitto_SOURCE_DIR}/libcommon"
		"${OPENSSL_INCLUDE_DIR}" # Required for cross compilation
)

if(WITH_BUNDLED_DEPS)
	target_include_directories(libmosquitto PRIVATE
		"${mosquitto_SOURCE_DIR}/deps"
	)
endif()

if(WITH_THREADING)
 if (WIN32)
    target_link_libraries(libmosquitto PUBLIC 
        ${OPENSSL_LIBRARIES} 
        ${PTHREAD_LIBRARIES}
        ws2_32
        crypt32)
 else()
    target_link_libraries(libmosquitto PUBLIC 
        ${OPENSSL_LIBRARIES} 
        ${PTHREAD_LIBRARIES})
 endif()
endif()

target_link_libraries(libmosquitto
	PUBLIC
		libmosquitto_common
)
target_link_libraries(libmosquitto PRIVATE ${LIBRARIES})

set_target_properties(libmosquitto PROPERTIES
	OUTPUT_NAME mosquitto
	VERSION ${VERSION}
	SOVERSION 1
	LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.version
	LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/linker.version"
	POSITION_INDEPENDENT_CODE 1
)

install(TARGETS libmosquitto
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

if(WITH_STATIC_LIBRARIES)
	add_library(libmosquitto_static STATIC ${C_SRC})
	if(WITH_PIC)
		set_target_properties(libmosquitto_static PROPERTIES
			POSITION_INDEPENDENT_CODE 1
		)
	endif()

	target_link_libraries(libmosquitto_static PRIVATE ${LIBRARIES})

	target_include_directories(libmosquitto_static PRIVATE
		"${mosquitto_SOURCE_DIR}"
		"${mosquitto_SOURCE_DIR}/include"
		"${mosquitto_SOURCE_DIR}/lib"
	)

	set_target_properties(libmosquitto_static PROPERTIES
		OUTPUT_NAME mosquitto_static
		VERSION ${VERSION}
	)

	target_compile_definitions(libmosquitto_static PUBLIC "LIBMOSQUITTO_STATIC")
	install(TARGETS libmosquitto_static
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	)
endif()
