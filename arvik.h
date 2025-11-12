// arvik.h

#ifndef ARVIK_H
#define ARVIK_H

// archive magic tag, written at the front of every archive
#define ARVIK_TAG "!<arvik>\n"

// per-member header field widths (bytes on disk)
#define ARVIK_NAME_LEN 16
#define ARVIK_DATE_LEN 12
#define ARVIK_UID_LEN   6
#define ARVIK_GID_LEN   6
#define ARVIK_MODE_LEN  8
#define ARVIK_SIZE_LEN 10

// the name field is padded and ends with this terminator byte
#define ARVIK_NAME_TERM '<'

// end-of-header / end-of-footer terminator
#define ARVIK_TERM     "`\n"
#define ARVIK_TERM_LEN 2

// exit codes
#define BAD_TAG      2
#define READ_FAIL    3
#define EXTRACT_FAIL 4

// one member header, written and read as a fixed-size block
typedef struct arvik_header
{
    char arvik_name[ARVIK_NAME_LEN];
    char arvik_date[ARVIK_DATE_LEN];
    char arvik_uid[ARVIK_UID_LEN];
    char arvik_gid[ARVIK_GID_LEN];
    char arvik_mode[ARVIK_MODE_LEN];
    char arvik_size[ARVIK_SIZE_LEN];
    char arvik_term[ARVIK_TERM_LEN];
} arvik_header_t;

// one member footer, holding the MD4 checksums of the header and the data as hex
typedef struct arvik_footer
{
    char md4sum_header[MD4_DIGEST_LENGTH * 2];
    char md4sum_data[MD4_DIGEST_LENGTH * 2];
    char arvik_term[ARVIK_TERM_LEN];
} arvik_footer_t;

#endif // ARVIK_H
