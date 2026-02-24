#ifndef __STATUSCODE_H__
#define __STATUSCODE_H__

#include <stdint.h>

typedef int status_code_t;
namespace server
{
    namespace core
    {
        namespace status
        {
            enum status_codes
            {
                STATUS_OK,
                STATUS_NULL,
                STATUS_UNSPECIFIED,
                STATUS_LOADING,
                STATUS_IN_PROCESS,
                STATUS_RUNNING,
                STATUS_UNKNOWN_ERR,
                STATUS_NO_MEM,
                STATUS_NOT_FOUND,
                STATUS_IO_ERROR,
                STATUS_NO_FILE,
                STATUS_EOF,
                STATUS_BAD_ARGUMENTS,
                STATUS_NOT_IMPLEMENTED,
                //
                STATUS_TOTAL,
                STATUS_MAX = STATUS_TOTAL - 1,
                STATUS_SUCCESS = STATUS_OK
            };

            const char* get_status(status_code_t code);
            bool status_is_success(status_code_t code);
            bool status_is_error(status_code_t code);
        }

    }
}

#endif
