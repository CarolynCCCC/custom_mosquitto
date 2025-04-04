#include "config.h"
#include <stdio.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

int init_datatype_read_tests(void);
int init_datatype_write_tests(void);
int init_property_read_tests(void);
int init_property_user_read_tests(void);
int init_property_write_tests(void);

int main(int argc, char *argv[])
{
	unsigned int fails;

	UNUSED(argc);
	UNUSED(argv);

    if(CU_initialize_registry() != CUE_SUCCESS){
        printf("Error initializing CUnit registry.\n");
        return 1;
    }

    if(0
			|| init_datatype_read_tests()
			|| init_datatype_write_tests()
			|| init_property_read_tests()
			|| init_property_user_read_tests()
			|| init_property_write_tests()
			){

        CU_cleanup_registry();
        return 1;
    }

    CU_basic_set_mode(CU_BRM_NORMAL);
    CU_basic_run_tests();
	fails = CU_get_number_of_failures();
    CU_cleanup_registry();

    return (int)fails;
}

