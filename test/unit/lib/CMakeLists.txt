add_executable(lib-test
	datatype_read.c
	datatype_write.c
	property_read.c
	property_user_read.c
	property_write.c
	stubs.c
	# main test files
	test.c
	../../../lib/packet_datatypes.c
	../../../lib/packet_mosq.c
	../../../lib/property_mosq.c
	../../../lib/util_mosq.c
)

target_include_directories(lib-test PRIVATE ${mosquitto_SOURCE_DIR}/libcommon)
if(WIN32)
    target_link_libraries(lib-test PRIVATE common-unit-test-header libmosquitto_common OpenSSL::SSL ws2_32)
else()
    target_link_libraries(lib-test PRIVATE common-unit-test-header libmosquitto_common OpenSSL::SSL)
endif()
add_test(NAME unit-lib-test COMMAND lib-test)
