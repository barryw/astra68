#include <astra/result.h>

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(astra_result_from_service(ASTRA_STATUS_EXISTS) ==
           ASTRA_ERROR_EXISTS);
    assert(astra_status_from_result(ASTRA_ERROR_EXISTS) ==
           ASTRA_STATUS_EXISTS);
    assert(strcmp(astra_result_text(ASTRA_ERROR_EXISTS),
                  "Item already exists") == 0);
    return 0;
}
