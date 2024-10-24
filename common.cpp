#include <cstddef>  // size_t の定義
#include <cstdlib>  // malloc, free の定義
#include <cstdio>   // snprintf の定義
#include <cstring>  // strcpy, strnlen などの定義
#include <doca_error.h>  // doca_error_t の定義

#include <doca_argp.h>
#include <doca_log.h>
#include <doca_apsh.h>
#include <doca_apsh_attr.h>

#include "common.h"
DOCA_LOG_REGISTER(APSH_COMMON);

doca_error_t
open_doca_device_with_pci(const char *pci_addr, tasks_check func, struct doca_dev **retval)
{
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    uint8_t is_addr_equal = 0;
    int res;
    size_t i;

    /* Set default return value */
    *retval = NULL;

    res = doca_devinfo_create_list(&dev_list, &nb_devs);
    if (res != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to load doca devices list. Doca_error value: %d", res);
        return static_cast<doca_error_t>(res);  // 修正箇所
    }

    /* Search */
    for (i = 0; i < nb_devs; i++) {
        res = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
        if (res == DOCA_SUCCESS && is_addr_equal) {
            /* If any special capabilities are needed */
            if (func != NULL && func(dev_list[i]) != DOCA_SUCCESS)
                continue;

            /* if device can be opened */
            res = doca_dev_open(dev_list[i], retval);
            if (res == DOCA_SUCCESS) {
                doca_devinfo_destroy_list(dev_list);
                return static_cast<doca_error_t>(res);  // 修正箇所
            }
        }
    }

    DOCA_LOG_WARN("Matching device not found");
    res = DOCA_ERROR_NOT_FOUND;

    doca_devinfo_destroy_list(dev_list);
    return static_cast<doca_error_t>(res);  // 修正箇所
}

char *
hex_dump(const void *data, size_t size)
{
    const size_t line_size = 8 + 2 + 8 * 3 + 1 + 8 * 3 + 1 + 16 + 1;
    size_t i, j, r, read_index;
    size_t num_lines, buffer_size;
    char *buffer, *write_head;
    unsigned char cur_char, printable;
    char ascii_line[17];
    const unsigned char *input_buffer;

    /* Allocate a dynamic buffer to hold the full result */
    num_lines = (size + 16 - 1) / 16;
    buffer_size = num_lines * line_size + 1;
    buffer = (char *)malloc(buffer_size);
    if (buffer == NULL)
        return NULL;
    write_head = buffer;
    input_buffer = static_cast<const unsigned char*>(data);  // 修正箇所
    read_index = 0;

    for (i = 0; i < num_lines; i++) {
        /* Offset */
        snprintf(write_head, buffer_size, "%08lX: ", i * 16);
        write_head += 8 + 2;
        buffer_size -= 8 + 2;
        /* Hex print - 2 chunks of 8 bytes */
        for (r = 0; r < 2; r++) {
            for (j = 0; j < 8; j++) {
                /* If there is content to print */
                if (read_index < size) {
                    cur_char = input_buffer[read_index++];
                    snprintf(write_head, buffer_size, "%02X ", cur_char);
                    /* Printable chars go "as-is" */
                    if (' ' <= cur_char && cur_char <= '~')
                        printable = cur_char;
                    /* Otherwise, use a '.' */
                    else
                        printable = '.';
                /* Else, just use spaces */
                } else {
                    snprintf(write_head, buffer_size, "   ");
                    printable = ' ';
                }
                ascii_line[r * 8 + j] = printable;
                write_head += 3;
                buffer_size -= 3;
            }
            /* Spacer between the 2 hex groups */
            snprintf(write_head, buffer_size, " ");
            write_head += 1;
            buffer_size -= 1;
        }
        /* Ascii print */
        ascii_line[16] = '\0';
        snprintf(write_head, buffer_size, "%s\n", ascii_line);
        write_head += 16 + 1;
        buffer_size -= 16 + 1;
    }
    /* No need for the last '\n' */
    write_head[-1] = '\0';
    return buffer;
}

