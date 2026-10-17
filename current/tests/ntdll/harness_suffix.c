uint32_t wine_server_call(void *request)
{
    const struct get_directory_entries_request *req = request;
    uint32_t available, count, i, offset = 0;

    server_calls++;
    last_index = req->index;
    last_max_count = req->max_count;
    active_reply->total_len = 0;
    active_reply->count = 0;
    if (fake_error) return fake_error;

    available = req->index < 3 ? 3 - req->index : 0;
    count = available < req->max_count ? available : req->max_count;
    for (i = 0; i < count; i++)
    {
        struct directory_entry entry = { sizeof(WCHAR), sizeof(WCHAR) };
        WCHAR name = (WCHAR)('A' + req->index + i);
        WCHAR type = (WCHAR)'D';
        uint32_t needed = sizeof(entry) + sizeof(name) + sizeof(type);

        if (offset + needed > active_reply_capacity) break;
        memcpy((char *)active_reply_data + offset, &entry, sizeof(entry));
        memcpy((char *)active_reply_data + offset + sizeof(entry), &name, sizeof(name));
        memcpy((char *)active_reply_data + offset + sizeof(entry) + sizeof(name), &type, sizeof(type));
        offset += needed;
        active_reply->total_len += entry.name_len + entry.type_len;
        active_reply->count++;
    }
    if (active_reply->count < count) return STATUS_MORE_ENTRIES;
    return STATUS_SUCCESS;
}

/* Calls the extracted function with a poisoned upper 24 bits in fifth arg R8. */
__asm__(
    ".text\n"
    ".globl _call_poisoned\n"
    "_call_poisoned:\n"
    "pushq %rbp\n"
    "movq %rsp, %rbp\n"
    "subq $32, %rsp\n"
    "movl %edi, -4(%rbp)\n"
    "movl %esi, -8(%rbp)\n"
    "movq %rdx, -16(%rbp)\n"
    "movq %rcx, -24(%rbp)\n"
    "movq $1, %rdi\n"
    "leaq _test_storage(%rip), %rsi\n"
    "movl _test_size(%rip), %edx\n"
    "movl -8(%rbp), %ecx\n"
    "movl $0xa5b6c700, %r8d\n"
    "movb -4(%rbp), %r8b\n"
    "movq -16(%rbp), %r9\n"
    "movq -24(%rbp), %rax\n"
    "movq %rax, (%rsp)\n"
    "call _NtQueryDirectoryObject\n"
    "leave\n"
    "retq\n"
);

extern NTSTATUS call_poisoned(ULONG low_byte, ULONG single_entry, ULONG *context, ULONG *ret_size);

static void reset_fake(void)
{
    fake_error = 0;
    last_index = UINT32_MAX;
    last_max_count = 0;
    server_calls = 0;
    test_size = sizeof(test_storage.bytes);
    active_reply_data = NULL;
    active_reply_capacity = 0;
    memset(&test_storage, 0, sizeof(test_storage));
}

static WCHAR first_name(void)
{
    const DIRECTORY_BASIC_INFORMATION *buffer = (const DIRECTORY_BASIC_INFORMATION *)test_storage.bytes;
    return buffer[0].ObjectName.Buffer ? buffer[0].ObjectName.Buffer[0] : 0;
}

static void test_nonprogress_sequence(void)
{
    ULONG context = 0, ret_size = 0;
    NTSTATUS status;

    reset_fake();
    status = call_poisoned(0x00, 1, &context, &ret_size);
    if (!(status == STATUS_SUCCESS && last_index == 0 && last_max_count == 1 && context == 1 && first_name() == 'A'))
        fprintf(stderr, "first call: status=%08x index=%u max=%u context=%u name=%04x calls=%u cap=%u\n",
                status, last_index, last_max_count, context, first_name(), server_calls, active_reply_capacity);
    assert(status == STATUS_SUCCESS && last_index == 0 && last_max_count == 1 && context == 1 && first_name() == 'A');

    status = call_poisoned(0x00, 1, &context, &ret_size);
    assert(status == STATUS_SUCCESS && last_index == 1 && last_max_count == 1 && context == 2 && first_name() == 'B');

    status = call_poisoned(0x00, 1, &context, &ret_size);
    assert(status == STATUS_SUCCESS && last_index == 2 && context == 3 && first_name() == 'C');

    status = call_poisoned(0x00, 1, &context, &ret_size);
    assert(status == STATUS_NO_MORE_ENTRIES && last_index == 3 && context == 3);
    assert(server_calls == 4 && ret_size == sizeof(DIRECTORY_BASIC_INFORMATION));
}

static void test_restart_truth_and_nonzero_byte(void)
{
    ULONG context = 3, ret_size = 0;
    NTSTATUS status;

    reset_fake();
    status = call_poisoned(0x01, 1, &context, &ret_size);
    assert(status == STATUS_SUCCESS && last_index == 0 && context == 1 && first_name() == 'A');

    context = 3;
    status = call_poisoned(0x80, 1, &context, &ret_size);
    assert(status == STATUS_SUCCESS && last_index == 0 && context == 1 && first_name() == 'A');
}

static void test_multi_entry_and_eof(void)
{
    ULONG context = 2, ret_size = 0;
    NTSTATUS status;
    const DIRECTORY_BASIC_INFORMATION *buffer;

    reset_fake();
    status = call_poisoned(1, 0, &context, &ret_size);
    buffer = (const DIRECTORY_BASIC_INFORMATION *)test_storage.bytes;
    assert(status == STATUS_SUCCESS && last_index == 0 && last_max_count == UINT32_MAX && context == 3);
    assert(buffer[0].ObjectName.Buffer[0] == 'A');
    assert(buffer[1].ObjectName.Buffer[0] == 'B');
    assert(buffer[2].ObjectName.Buffer[0] == 'C');
    assert(buffer[3].ObjectName.Buffer == NULL);
}

static void test_error_and_small_buffer(void)
{
    ULONG context = 2, ret_size = 0;
    NTSTATUS status;

    reset_fake();
    fake_error = STATUS_ACCESS_DENIED;
    status = call_poisoned(0, 1, &context, &ret_size);
    assert(status == STATUS_ACCESS_DENIED && context == 2 && last_index == 2);

    reset_fake();
    test_size = sizeof(DIRECTORY_BASIC_INFORMATION);
    status = call_poisoned(0, 1, &context, &ret_size);
    assert(status == STATUS_BUFFER_TOO_SMALL && context == 2 && last_index == 2);
    assert(ret_size > test_size);
}

int main(void)
{
    _Static_assert(sizeof(BOOLEAN) == 1, "BOOLEAN must be one byte");
    _Static_assert(sizeof(DIRECTORY_BASIC_INFORMATION) == 32, "unexpected 64-bit DBI layout");
    test_nonprogress_sequence();
    test_restart_truth_and_nonzero_byte();
    test_multi_entry_and_eof();
    test_error_and_small_buffer();
    puts("NtQueryDirectoryObject extracted-function harness: PASS");
    return 0;
}
