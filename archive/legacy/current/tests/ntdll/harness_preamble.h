#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t BOOLEAN;
typedef uint32_t ULONG;
typedef uint32_t NTSTATUS;
typedef uint32_t obj_handle_t;
typedef uint16_t WCHAR;
typedef void *HANDLE;

#define WINAPI
#define STATUS_SUCCESS 0x00000000u
#define STATUS_MORE_ENTRIES 0x00000105u
#define STATUS_NO_MEMORY 0xc0000017u
#define STATUS_BUFFER_TOO_SMALL 0xc0000023u
#define STATUS_NO_MORE_ENTRIES 0x8000001au
#define STATUS_ACCESS_DENIED 0xc0000022u

typedef struct
{
    uint16_t Length;
    uint16_t MaximumLength;
    WCHAR *Buffer;
} UNICODE_STRING;

typedef struct
{
    UNICODE_STRING ObjectName;
    UNICODE_STRING ObjectTypeName;
} DIRECTORY_BASIC_INFORMATION;

struct directory_entry
{
    uint32_t name_len;
    uint32_t type_len;
};

struct request_header
{
    uint32_t req;
    uint32_t request_size;
    uint32_t reply_size;
};

struct reply_header
{
    uint32_t status;
    uint32_t reply_size;
};

struct get_directory_entries_request
{
    struct request_header __header;
    obj_handle_t handle;
    uint32_t index;
    uint32_t max_count;
};

struct get_directory_entries_reply
{
    struct reply_header __header;
    uint32_t total_len;
    uint32_t count;
};

static struct get_directory_entries_reply *active_reply;
static void *active_reply_data;
static uint32_t active_reply_capacity;
static uint32_t fake_error;
static uint32_t last_index;
static uint32_t last_max_count;
static uint32_t server_calls;
uint32_t test_size = 512;

union aligned_buffer
{
    max_align_t alignment;
    unsigned char bytes[512];
};

union aligned_buffer test_storage;

static inline obj_handle_t wine_server_obj_handle(HANDLE handle)
{
    return (obj_handle_t)(uintptr_t)handle;
}

static inline void wine_server_set_reply(void *request, void *data, uint32_t capacity)
{
    struct get_directory_entries_request *req = request;
    req->__header.reply_size = capacity;
    active_reply_data = data;
    active_reply_capacity = capacity;
}

static uint32_t wine_server_call(void *request);

#define SERVER_START_REQ(type) \
    do { \
        struct type##_request req_storage = {0}; \
        struct type##_reply reply_storage = {0}; \
        struct type##_request *const req = &req_storage; \
        const struct type##_reply *const reply = &reply_storage; \
        active_reply = &reply_storage; \
        do

#define SERVER_END_REQ \
        while (0); \
        active_reply = NULL; \
    } while (0)

