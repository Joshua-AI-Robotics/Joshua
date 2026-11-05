// Native constants; only success versus unsupported access matters to tests.
#pragma once
#define ABORTIDX_UNSUPPORTED_ACCESS 1u
#define EC_API_eERR_INVALID 1u
#define DEFTYPE_OCTETSTRING 0x000au
#define DEFTYPE_UNSIGNED32 0x0007u
#define ACCESS_READ 1u
#define ACCESS_WRITE 2u
#define ACCESS_READWRITE (ACCESS_READ | ACCESS_WRITE)
#define OBJACCESS_RXPDOMAPPING 4u
#define OBJACCESS_TXPDOMAPPING 8u
