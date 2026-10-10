#include "support.h"

static char status_text[512] = "Not initialized";

void memory_copy(void *destination, const void *source, size_t size) {
    volatile unsigned char *destination_bytes = (volatile unsigned char *)destination;
    const unsigned char *source_bytes = (const unsigned char *)source;
    while (size--) {
        *destination_bytes++ = *source_bytes++;
    }
}

int memory_equal(const void *left, const void *right, size_t size) {
    const unsigned char *left_bytes = left, *right_bytes = right;
    while (size--) {
        if (*left_bytes++ != *right_bytes++) {
            return 0;
        }
    }
    return 1;
}

size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length]) {
        ++length;
    }
    return length;
}

void status_set(const char *message) {
    size_t length = text_length(message);
    if (length >= sizeof(status_text)) {
        length = sizeof(status_text) - 1;
    }
    memory_copy(status_text, message, length);
    status_text[length] = 0;
}

void status_append(const char *message) {
    size_t length = text_length(status_text), append_size = text_length(message);
    if (append_size >= sizeof(status_text) - length) {
        append_size = sizeof(status_text) - length - 1;
    }
    memory_copy(status_text + length, message, append_size);
    status_text[length + append_size] = 0;
}

int text_equal_ignore_case(const char *left, const char *right) {
    while (*left && *right) {
        char left_char = *left++, right_char = *right++;
        if (left_char >= 'A' && left_char <= 'Z') {
            left_char += 'a' - 'A';
        }
        if (right_char >= 'A' && right_char <= 'Z') {
            right_char += 'a' - 'A';
        }
        if (left_char != right_char) {
            return 0;
        }
    }
    return *left == *right;
}

int memory_read(const void *address, void *out, size_t size) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, out, size, &count) &&
           count == size;
}

int memory_is_writable(const void *address, size_t size) {
    uintptr_t start = (uintptr_t)address, end = start + size;
    if (!start || end < start) {
        return 0;
    }
    while (start < end) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)start, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            return 0;
        }
        DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) {
            return 0;
        }
        uintptr_t next = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (next <= start) {
            return 0;
        }
        start = next;
    }
    return 1;
}

const char *status_get(void) {
    return status_text;
}

int path_join(wchar_t *output, const wchar_t *directory, const wchar_t *name) {
    size_t length = 0;
    while (directory[length]) {
        if (length >= 32760) {
            return 0;
        }
        output[length] = directory[length];
        ++length;
    }
    if (length && output[length - 1] != L'\\' && output[length - 1] != L'/') {
        output[length++] = L'\\';
    }
    for (size_t name_index = 0;; ++name_index) {
        if (length >= 32767) {
            return 0;
        }
        output[length++] = name[name_index];
        if (!name[name_index]) {
            break;
        }
    }
    return 1;
}
