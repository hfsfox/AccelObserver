#include <core/common/statuscode.h>

namespace
{
    const char *status_descriptions[] =
    {
        "OK",
        "NULL",
        "Unspecified",
        "Loading",
        "In process",
        "Running",
        "Unknown error",
        "No memory",
        "Not found",
        "I/O error",
        "No file",
        "End of file",
        "Bad arguments",
        "Not implemented",
        //
        NULL
    };
}

namespace server
{
    namespace core
    {
        namespace status
        {
            const char* get_status(status_code_t code)
            {
                return ((code >= 0) && (code < STATUS_TOTAL)) ? status_descriptions[code] : NULL;
            };
            bool status_is_success(status_code_t code)
            {
                return code == STATUS_OK;
            };
            bool status_is_error(status_code_t code)
            {
                if (status_is_success(code))
                    return true;

                return false;
            };
        }
    }
}
