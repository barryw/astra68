#include "../policy.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    AstraCredentialRequest request = {0};
    request.header.total_size = sizeof(request);
    request.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    request.header.protocol = ASTRA_CREDENTIAL_PROTOCOL;
    request.header.protocol_version = ASTRA_CREDENTIAL_VERSION;
    request.header.transaction_id = 1u;
    strcpy(request.key, "app.story.save");
    request.header.operation = ASTRA_CREDENTIAL_SET;
    request.length = ASTRA_CREDENTIAL_VALUE_MAX;
    assert(credential_request_valid(&request, sizeof(request), 2u, 0u));
    assert(!credential_request_valid(&request, sizeof(request), 2u, 1u));
    assert(!credential_request_valid(&request, sizeof(request), 1u, 0u));
    request.length++;
    assert(!credential_request_valid(&request, sizeof(request), 2u, 0u));
    request.header.operation = ASTRA_CREDENTIAL_GET;
    request.length = 0u;
    assert(credential_request_valid(&request, sizeof(request), 1u, 1u));
    assert(!credential_request_valid(&request, sizeof(request), 1u, 0u));
    request.header.operation = ASTRA_CREDENTIAL_REMOVE;
    assert(credential_request_valid(&request, sizeof(request), 1u, 0u));
    request.key[sizeof("app.story.save")] = 'x';
    assert(!credential_request_valid(&request, sizeof(request), 1u, 0u));
    request.key[sizeof("app.story.save")] = '\0';
    memset(request.key, 'x', sizeof(request.key));
    assert(!credential_request_valid(&request, sizeof(request), 1u, 0u));
    return 0;
}
