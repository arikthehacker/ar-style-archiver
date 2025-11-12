// Ariella Marchuk
// amarchuk@pdx.edu
// arvik-md4.c
// cs333 Lab 2 – UNIX file I/O with MD4
// supposed to support:
//   -c       create archive
//   -x       extract archive
//   -t       table of contents
//   -v       verbose (long table / chatter)
//   -V       validate MD4 for header + data
//   -f name  archive file name (else stdin/stdout)
//   -h       help text

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>

// MD4 library (for babbage: -lmd)
#include <md4.h>

#include "arvik.h"

// make sure we have a terminator char
#ifndef ARVIK_NAME_TERM
# define ARVIK_NAME_TERM '<'
#endif

// forward declarations
static void usage(void);
static void perms_from_mode(int mode, char out[11]);
static void skip_forward(int fd, off_t n);
static void toc_table(int iarch, int verbose);
static void md_to_hex(const unsigned char *dgst, char *out_hex);
static void validate_archive(int iarch, int verbose);
static void create_archive(int oarch, char **argv, int verbose);
static int  want_member(const char *stored_name, char **targets);
static void extract_archive(int iarch, int verbose, char **targets);

// copy src into fixedwidth text field padding with spaces
static void put_field(char *dst, size_t width, const char *src)
{
    size_t len = strlen(src);
    if (len > width)
        len = width;

    memcpy(dst, src, len);
    if (len < width) { memset(dst + len, ' ', width - len); }
}

// show help text (-h)
static void usage(void)
{
    printf("Usage: arvik-md4 -[cxtvVf:h] archive-file file...\n");
    printf("        -c           create a new archive file\n");
    printf("        -x           extract members from an existing archive file\n");
    printf("        -t           show the table of contents of archive file\n");
    printf("        -f filename  name of archive file to use\n");
    printf("        -V           Validate the md4 values for the header and data\n");
    printf("        -v           verbose output\n");
    printf("        -h           show help text\n");
}

// build a rwx string ...like rwxr-xr--?
static void perms_from_mode(int mode, char out[11])
{
    int i;
    const char rwx[] = "rwxrwxrwx";
    for (i = 0; i < 9; i++) { out[i] = (mode & (1 << (8 - i))) ? rwx[i] : '-'; }
    out[9] = '\0';
}

// try to lseek. if fails pipe  fifo read and discard
static void skip_forward(int fd, off_t n)
{
    // first try lseek
    if (lseek(fd, n, SEEK_CUR) == (off_t)-1)
    {
        // fall back to read/toss
        char dump[8192];

        while (n > 0)
        {
            size_t want = (n > (off_t)sizeof(dump)) ? sizeof(dump) : (size_t)n;
            ssize_t r = read(fd, dump, want);

            if (r <= 0)
            {
                perror("read while skipping");
                exit(EXIT_FAILURE);
            }

            n -= r;
        }
    }
}

// table of contents (-t)
static void toc_table(int iarch, int verbose)
{
    char tagbuf[64];
    size_t tlen;

    tlen = strlen(ARVIK_TAG);
    memset(tagbuf, 0, sizeof(tagbuf));

    // check tag
    if (read(iarch, tagbuf, tlen) != (ssize_t)tlen || strncmp(tagbuf, ARVIK_TAG, tlen) != 0)
    {
        fprintf(stderr, "invalid arvik file (bad or short tag)\n");
        exit(BAD_TAG);
    }

    // walk all members
    for (;;)
    {
        arvik_header_t md;
        arvik_footer_t mf;
        ssize_t r;

        char stored[ARVIK_NAME_LEN + 1];
        char *term;
        const char *base;
        long uid;
        long gid;
        long long size;
        unsigned long mode;
        time_t mtime;
        int padding;

        // try to read a header
        r = read(iarch, &md, sizeof(md));
        if (r == 0)
        {
            // EOF, done
            break;
        }
        if (r < 0)
        {
            perror("read header");
            exit(READ_FAIL);
        }
        if (r != (ssize_t)sizeof(md))
        {
            fprintf(stderr, "truncated arvik header\n");
            exit(READ_FAIL);
        }

        // ;;;;; parse header fields

        // name (strip ARVIK_NAME_TERM and any path)
        memset(stored, 0, sizeof(stored));
        strncpy(stored, md.arvik_name, ARVIK_NAME_LEN);
        term = strchr(stored, ARVIK_NAME_TERM);
        if (term != NULL)
        {
            *term = '\0';
        }

        base = strrchr(stored, '/');
        if (base != NULL)
        {
            base++;
        }
        else
        {
            base = stored;
        }

        uid   = strtol(md.arvik_uid,  NULL, 10);
        gid   = strtol(md.arvik_gid,  NULL, 10);
        size  = strtoll(md.arvik_size, NULL, 10);
        mode  = strtoul(md.arvik_mode, NULL, 8);
        mtime = (time_t)strtol(md.arvik_date, NULL, 10);

        padding = (size % 2 == 0) ? 0 : 1;

        // skip data + padding to reach footer
        if (lseek(iarch, (off_t)(size + padding), SEEK_CUR) == (off_t)-1)
        {
            perror("lseek data");
            exit(READ_FAIL);
        }

        // read footer for md4 values
        r = read(iarch, &mf, sizeof(mf));
        if (r != (ssize_t)sizeof(mf))
        {
            fprintf(stderr, "truncated arvik footer\n");
            exit(READ_FAIL);
        }

        // ;;;;;; output

        if (!verbose)
        {
            // short TOC
            printf("%s\n", base);
        }
        else
        {
            char perm[11];
            struct tm *tm_ptr;
            char timebuf[64];
            struct passwd *pw;
            struct group  *gr;
            const char *uname;
            const char *gname;

            // build permissions string from mode
            perms_from_mode((int)(mode & 0777), perm);

            tm_ptr = localtime(&mtime);
            if (tm_ptr == NULL)
            {
                perror("localtime");
                exit(EXIT_FAILURE);
            }
            strftime(timebuf, sizeof(timebuf), "%b %e %H:%M %Y", tm_ptr);

            pw = getpwuid((uid_t)uid);
            gr = getgrgid((gid_t)gid);
            uname = (pw != NULL) ? pw->pw_name : "?";
            gname = (gr != NULL) ? gr->gr_name : "?";

            printf("file name: %s\n", base);
            printf("    mode:       %.9s\n", perm);
            printf("    uid: %16ld  %s\n", uid, uname);
            printf("    gid: %16ld  %s\n", gid, gname);
            printf("    size: %16lld  bytes\n", size);
            printf("    mtime:      %s\n", timebuf);
            printf("    header md4: %.*s\n", MD4_DIGEST_LENGTH * 2, mf.md4sum_header);
            printf("    data md4:   %.*s\n", MD4_DIGEST_LENGTH * 2, mf.md4sum_data);
        }
    }
}

// convert raw MD4 bytes into lowercase hex string
static void md_to_hex(const unsigned char *dgst, char *out_hex)
{
    static const char *hex = "0123456789abcdef";
    int i;

    for (i = 0; i < MD4_DIGEST_LENGTH; ++i)
    {
        unsigned char b = dgst[i];
        out_hex[i * 2]     = hex[(b >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex[b & 0xF];
    }
    out_hex[MD4_DIGEST_LENGTH * 2] = '\0';
}

// validate archive (-V)
static void validate_archive(int iarch, int verbose)
{
    char tagbuf[64] = { '\0' };
    size_t tlen = strlen(ARVIK_TAG);
    arvik_header_t md;
    arvik_footer_t mf;
    unsigned char dbytes[MD4_DIGEST_LENGTH];
    char hex_hdr[MD4_DIGEST_LENGTH * 2 + 1];
    char hex_dat[MD4_DIGEST_LENGTH * 2 + 1];
    char namebuf[256] = { '\0' };
    char *term_pos = NULL;
    char *base = NULL;
    int failures = 0;

    // confirm tag
    if (read(iarch, tagbuf, tlen) != (ssize_t)tlen || strncmp(tagbuf, ARVIK_TAG, tlen) != 0)
    {
        fprintf(stderr, "invalid arvik file (bad or short tag)\n");
        exit(EXIT_FAILURE);
    }

    // walk members
    while (1)
    {
        ssize_t r;
        int size;
        int padding;
        int header_ok;
        int data_ok;
        MD4_CTX ctx;

        // read header
        r = read(iarch, &md, sizeof(arvik_header_t));
        if (r == 0)
        {
            break;
        }
        if (r < 0)
        {
            perror("read header");
            exit(EXIT_FAILURE);
        }
        if (r != (ssize_t)sizeof(arvik_header_t))
        {
            fprintf(stderr, "truncated arvik header\n");
            exit(EXIT_FAILURE);
        }

        // extract member name for messages
        memset(namebuf, 0, sizeof(namebuf));
        strncpy(namebuf, md.arvik_name, ARVIK_NAME_LEN);
        term_pos = strchr(namebuf, ARVIK_NAME_TERM);
        if (term_pos != NULL)
        {
            *term_pos = '\0';
        }
        base = strrchr(namebuf, '/');
        base = (base != NULL) ? base + 1 : namebuf;

        // MD4 of header bytes as stored
        MD4Init(&ctx);
        MD4Update(&ctx, (const unsigned char *)&md, sizeof(arvik_header_t));
        MD4Final(dbytes, &ctx);
        md_to_hex(dbytes, hex_hdr);

        // MD4 of data bytes
        size = (int)strtol(md.arvik_size, NULL, 10);
        MD4Init(&ctx);
        {
            char buf[8192];
            int remaining = size;

            while (remaining > 0)
            {
                size_t want = (remaining > (int)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
                r = read(iarch, buf, want);
                if (r <= 0)
                {
                    perror("read data for md4");
                    exit(EXIT_FAILURE);
                }

                MD4Update(&ctx, (const unsigned char *)buf, (size_t)r);
                remaining -= (int)r;
            }
        }
        MD4Final(dbytes, &ctx);
        md_to_hex(dbytes, hex_dat);

        // skip padding byte 
        padding = (size % 2 == 0) ? 0 : 1;
        if (padding)
        {
            char junk;
            if (read(iarch, &junk, 1) != 1)
            {
                fprintf(stderr, "truncated padding byte\n");
                exit(EXIT_FAILURE);
            }
        }

        // read footer
        r = read(iarch, &mf, sizeof(arvik_footer_t));
        if (r != (ssize_t)sizeof(arvik_footer_t))
        {
            fprintf(stderr, "truncated arvik footer\n");
            exit(EXIT_FAILURE);
        }

        // compare MD4s 
        header_ok = (strncasecmp(hex_hdr, mf.md4sum_header, MD4_DIGEST_LENGTH * 2) == 0);
        data_ok   = (strncasecmp(hex_dat, mf.md4sum_data, MD4_DIGEST_LENGTH * 2) == 0);

        if (header_ok && data_ok) { if (verbose) { printf("ok %s\n", base); } }
        else
        {
            failures++;

            if (!header_ok)
            {
                printf("header md4 mismatch: %s\ncalc: %s\nfile: %.*s\n", base, hex_hdr, MD4_DIGEST_LENGTH * 2, mf.md4sum_header);
            }
            if (!data_ok)
            {
                printf("data md4 mismatch:   %s\ncalc: %s\nfile: %.*s\n", base, hex_dat, MD4_DIGEST_LENGTH * 2, mf.md4sum_data);
            }
        }
    }

    // non-zero exit if anything failed
    if (failures > 0) { exit(EXIT_FAILURE);}
}

// create archive (-c)
static void create_archive(int oarch, char **argv, int verbose)
{
    // write tag at start of archive
    {
        size_t tlen = strlen(ARVIK_TAG);
        ssize_t w = write(oarch, ARVIK_TAG, tlen);

        if (w != (ssize_t)tlen)
        {
            perror("write tag");
            exit(EXIT_FAILURE);
        }
    }

    // loop over member paths
    while (*argv != NULL)
    {
        const char *path = *argv;
        struct stat st;
        int ifd;
        arvik_header_t mh;
        arvik_footer_t mf;
        MD4_CTX ctx;
        unsigned char dbytes[MD4_DIGEST_LENGTH];
        char hex_hdr[MD4_DIGEST_LENGTH * 2 + 1];
        char hex_dat[MD4_DIGEST_LENGTH * 2 + 1];
        char namebuf[ARVIK_NAME_LEN + 1];
        ssize_t w;
        int padding_needed;

        // stat member
        if (stat(path, &st) == -1)
        {
            perror(path);
            exit(EXIT_FAILURE);
        }

        // open member for reading
        ifd = open(path, O_RDONLY);
        if (ifd == -1)
        {
            perror(path);
            exit(EXIT_FAILURE);
        }

        // initialize header with spaces
        memset(&mh, ' ', sizeof(mh));

        // set terminator field to ARVIK_TERM
        memcpy(mh.arvik_term, ARVIK_TERM, ARVIK_TERM_LEN);

        // build name field with terminator
        {
            size_t namelen;
            const char *p = path;
            size_t i;

            namelen = strlen(p);
            if (namelen >= (size_t)ARVIK_NAME_LEN) { namelen = (size_t)ARVIK_NAME_LEN - 1; }

            memset(namebuf, 0, sizeof(namebuf));
            strncpy(namebuf, p, namelen);
            namebuf[namelen] = '\0';

            // fill with spaces then copy name
            for (i = 0; i < (size_t)ARVIK_NAME_LEN; ++i) { mh.arvik_name[i] = ' '; }
            for (i = 0; i < namelen && i < (size_t)ARVIK_NAME_LEN; ++i) { mh.arvik_name[i] = namebuf[i]; }
            if (namelen < (size_t)ARVIK_NAME_LEN) { mh.arvik_name[namelen] = ARVIK_NAME_TERM; }
        }

        // numeric fields as ASCII strings
        {
            char tmp[64];

            // date (seconds since epoch decimal)
            snprintf(tmp, sizeof(tmp), "%ld", (long)st.st_mtime);
            put_field(mh.arvik_date, ARVIK_DATE_LEN, tmp);

            // uid (decimal)
            snprintf(tmp, sizeof(tmp), "%ld", (long)st.st_uid);
            put_field(mh.arvik_uid, ARVIK_UID_LEN, tmp);

            // gid (decimal)
            snprintf(tmp, sizeof(tmp), "%ld", (long)st.st_gid);
            put_field(mh.arvik_gid, ARVIK_GID_LEN, tmp);

            // mode (full st_mode in octal)
            snprintf(tmp, sizeof(tmp), "%o", (unsigned)st.st_mode);
            put_field(mh.arvik_mode, ARVIK_MODE_LEN, tmp);

            // size (decimal)
            snprintf(tmp, sizeof(tmp), "%lld", (long long)st.st_size);
            put_field(mh.arvik_size, ARVIK_SIZE_LEN, tmp);
        }


        // MD4 over header as stored
        MD4Init(&ctx);
        MD4Update(&ctx, (const unsigned char *)&mh, sizeof(arvik_header_t));
        MD4Final(dbytes, &ctx);
        md_to_hex(dbytes, hex_hdr);

        // write header
        w = write(oarch, &mh, sizeof(arvik_header_t));
        if (w != (ssize_t)sizeof(arvik_header_t))
        {
            perror("write header");
            exit(EXIT_FAILURE);
        }

        // MD4 + write data bytes
        MD4Init(&ctx);
        {
            char buf[8192];
            ssize_t r;

            while ((r = read(ifd, buf, sizeof(buf))) > 0)
            {
                ssize_t out = write(oarch, buf, (size_t)r);

                if (out != r)
                {
                    perror("write data");
                    exit(EXIT_FAILURE);
                }

                MD4Update(&ctx, (const unsigned char *)buf, (size_t)r);
            }

            if (r < 0)
            {
                perror("read data");
                exit(EXIT_FAILURE);
            }
        }
        MD4Final(dbytes, &ctx);
        md_to_hex(dbytes, hex_dat);

        // add padding newline if size is odd 
        padding_needed = ((st.st_size % 2) != 0);
        if (padding_needed)
        {
            char nl = '\n';
            if (write(oarch, &nl, 1) != 1)
            {
                perror("write padding");
                exit(EXIT_FAILURE);
            }
        }

        // build footer
        memset(&mf, ' ', sizeof(mf));
        memcpy(mf.md4sum_header, hex_hdr, MD4_DIGEST_LENGTH * 2);
        memcpy(mf.md4sum_data,   hex_dat, MD4_DIGEST_LENGTH * 2);
        memcpy(mf.arvik_term,    ARVIK_TERM, ARVIK_TERM_LEN);

        // write footer
        w = write(oarch, &mf, sizeof(arvik_footer_t));
        if (w != (ssize_t)sizeof(arvik_footer_t))
        {
            perror("write footer");
            exit(EXIT_FAILURE);
        }

        // close input member
        close(ifd);

        // verbose create line
        if (verbose)
        {
            const char *base2 = strrchr(path, '/');
            base2 = (base2 != NULL) ? base2 + 1 : path;
            printf("added %s\n", base2);
        }

        // next member
        ++argv;
    }
}

// decide if this member should be extracted
static int want_member(const char *stored_name, char **targets)
{
    // if no name filters given extract all
    if (targets == NULL || *targets == NULL) { return 1; }

    // compare against each target full and basename
    {
        const char *base = strrchr(stored_name, '/');

        if (base != NULL) { base++; }
        else { base = stored_name; }

        while (*targets != NULL)
        {
            const char *t = *targets;
            if (strcmp(stored_name, t) == 0) { return 1; }
            if (strcmp(base, t) == 0) { return 1; }
            ++targets;
        }
    }
    return 0;
}

// extract (-x) write members to files in cwd basename only
static void extract_archive(int iarch, int verbose, char **targets)
{
    // tag verification
    {
        char tagbuf[64];
        size_t tlen = strlen(ARVIK_TAG);

        memset(tagbuf, 0, sizeof(tagbuf));
        if (read(iarch, tagbuf, tlen) != (ssize_t)tlen || strncmp(tagbuf, ARVIK_TAG, tlen) != 0)
        {
            fprintf(stderr, "invalid arvik file (bad or short tag)\n");
            exit(BAD_TAG);
        }
    }
    // walk members 1 header 2 data 3 padding 4 footer
    for (;;)
    {
        arvik_header_t md;
        arvik_footer_t mf;
        ssize_t r;
        // try read header
        r = read(iarch, &md, sizeof(arvik_header_t));
        if (r == 0) { break; }
        if (r < 0)
        {
            perror("read header");
            exit(READ_FAIL);
        }
        if (r != (ssize_t)sizeof(arvik_header_t))
        {
            fprintf(stderr, "truncated arvik header\n");
            exit(READ_FAIL);
        }
         // parse member name
        {
            char stored[ARVIK_NAME_LEN + 1];
            char *term;
            const char *base;
            int do_extract;
            int out_fd;
            int size;
            int padding;
            int mode_oct;
            time_t mtime_val;
            struct utimbuf ut;

            // fetch name field and strip terminator
            memset(stored, 0, sizeof(stored));
            strncpy(stored, md.arvik_name, ARVIK_NAME_LEN);
            term = strchr(stored, ARVIK_NAME_TERM);
            if (term != NULL) {*term = '\0';}

            // decide if we want this member
            do_extract = want_member(stored, targets);

            // compute data size and padding now
            size = (int)strtol(md.arvik_size, NULL, 10);
            padding = (size % 2 == 0) ? 0 : 1;
            if (!do_extract)
            {
                // skip data and padding then footer
                skip_forward(iarch, (off_t)(size + padding));
                r = read(iarch, &mf, sizeof(arvik_footer_t));
                if (r != (ssize_t)sizeof(arvik_footer_t))
                {
                    fprintf(stderr, "truncated arvik footer\n");
                    exit(READ_FAIL);
                }
                continue;
            }
            // extract to basename for safety
            base = strrchr(stored, '/');
            if (base != NULL) { base++; }
            else { base = stored; }

            // open output file (truncate/overwrite)
            mode_oct = (int)strtol(md.arvik_mode, NULL, 8);
            mode_oct = mode_oct & 0777;
            out_fd = open(base, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (out_fd == -1)
            {
                perror(base);
                exit(EXTRACT_FAIL);
            }
            // stream data out
            {
                int remaining = size;
                char buf[8192];
                while (remaining > 0)
                {
                    size_t want;
                    ssize_t r2;

                    want = (remaining > (int)sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
                    r2 = read(iarch, buf, want);
                    if (r2 <= 0)
                    {
                        perror("read data");
                        exit(READ_FAIL);
                    }
                    if (write(out_fd, buf, (size_t)r2) != r2)
                    {
                        perror("write out");
                        exit(EXTRACT_FAIL);
                    }
                    remaining -= (int)r2;
                }
            }
            // consume padding byte if present
            if (padding)
            {
                char junk;
                if (read(iarch, &junk, 1) != 1)
                {
                    fprintf(stderr, "truncated padding byte\n");
                    exit(READ_FAIL);
                }
            }
            // read footer DOEs  NOT REVALIDATE  
            r = read(iarch, &mf, sizeof(arvik_footer_t));
            if (r != (ssize_t)sizeof(arvik_footer_t))
            {
                fprintf(stderr, "truncated arvik footer\n");
                exit(READ_FAIL);
            }
            // set file mode from header chmodddddd
            if (fchmod(out_fd, (mode_t)(mode_oct & 0777)) == -1) { perror("fchmod"); }

            // set mtime from header (utime)
            mtime_val = (time_t)strtol(md.arvik_date, NULL, 10);
            ut.actime = mtime_val;
            ut.modtime = mtime_val;
            if (utime(base, &ut) == -1) { perror("utime"); }

            // close output
            close(out_fd);

            // verbose extract line
            if (verbose) { printf("x %s\n", base);}
        }
    }
}

int main(int argc, char *argv[])
{
    int opt;
    int toc_flag      = 0;
    int verbose_flag  = 0;
    int validate_flag = 0;
    int create_flag   = 0;
    int extract_flag  = 0;
    char *archive_name = NULL;
    int arch_in_fd  = STDIN_FILENO;
    int arch_out_fd = STDOUT_FILENO;
    int leftover_start;
    int leftover_count;
    int modes;
    int i;
    char **extract_targets = NULL;
    char **create_members  = NULL;

    // ;;;;;;; parse options
    while ((opt = getopt(argc, argv, "ctxf:vVh")) != -1)
    {
        switch (opt)
        {
            case 'c':
                create_flag = 1;
                break;

            case 't':
                toc_flag = 1;
                break;

            case 'x':
                extract_flag = 1;
                break;

            case 'f':
                archive_name = optarg;
                break;

            case 'v':
                verbose_flag = 1;
                break;

            case 'V':
                validate_flag = 1;
                break;

            case 'h':
                usage();
                return EXIT_SUCCESS;

            default:
                usage();
                return EXIT_FAILURE;
        }
    }
  
    // ;;;;;;;; count modes
    modes = (create_flag != 0) + (toc_flag != 0) + (extract_flag != 0) + (validate_flag != 0);

    // ;;;;;;;;; 1 extract 2 create 3 validate 4 toc
    if (extract_flag && create_flag)
    {
        // multimode extracts from archive then create to stdout
        if (archive_name != NULL && access(archive_name, F_OK) != 0) { extract_flag = 0; }
        // IGNORE T AND V IN MULTIMODE 
        toc_flag = 0;
        validate_flag = 0;
    }
    else if (extract_flag && modes > 1)
    {
        // prioritize extract 
        create_flag = 0;
        toc_flag = 0;
        validate_flag = 0;
    }
    else if (modes > 1)
    {
        // error for multiple modes w/o extract 
        usage();
        return EXIT_FAILURE;
    }
    else if (modes == 0)
    {
        fprintf(stderr, "nothing to do (try -c, -t, -x, or -V)\n");
        return EXIT_FAILURE;
    }

    // ;;;;;;; leftover args
    leftover_start = optind;
    leftover_count = argc - optind;

    extract_targets = NULL;
    create_members  = NULL;

    // -x and -c together oooh special
    if (extract_flag && create_flag)
    {
        // same check if archive exists 
        if (archive_name != NULL && access(archive_name, F_OK) != 0) { extract_flag = 0;} 
        if (extract_flag && validate_flag)
        {
            fprintf(stderr, "cannot combine -V with -x and -c\n");
            return EXIT_FAILURE;
        }
    }

    // ;;;;;;;;;; handle multimode
    if (extract_flag && create_flag)
    {
        int sentinel_idx = -1;

        for (i = 0; i < leftover_count; ++i)
        {
            if (strcmp(argv[leftover_start + i], "-c") == 0)
            {
                sentinel_idx = i;
                break;
            }
        }

        if (sentinel_idx >= 0)
        {
            // BEFORE -c extracts targets
            // AFTER -c creates members 
            extract_targets = argv + leftover_start;

            // NULLTERMINATE extract targets at -c position
            argv[leftover_start + sentinel_idx] = NULL;

            if (sentinel_idx + 1 < leftover_count)
            {
                create_members = argv + leftover_start + sentinel_idx + 1;
            }
        }
        else
        {
            extract_targets = argv + leftover_start;
            create_flag = 0;
        }
    }
    else { if (extract_flag) { extract_targets = argv + optind; } }
     
    // ;;;;;;;; create only
    if (create_flag && !extract_flag)
    {
        if (archive_name != NULL)
        {
            arch_out_fd = open(archive_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (arch_out_fd == -1)
            {   perror(archive_name);
                return EXIT_SUCCESS; }
        }
        create_archive(arch_out_fd, argv + optind, verbose_flag);
        if (arch_out_fd != STDOUT_FILENO) { close(arch_out_fd); }
        return EXIT_SUCCESS;
    }

    // ;;;;;;; open archive for reading if -f is useddddddd
    if (archive_name != NULL)
    {
        arch_in_fd = open(archive_name, O_RDONLY);
        if (arch_in_fd == -1)
        {   perror(archive_name);
            return EXIT_FAILURE; }
    }

    // ;;;;;;;;; multimode extract&&&create 
    if (extract_flag && create_flag)
    {
        extract_archive(arch_in_fd, verbose_flag, extract_targets);
        if (arch_in_fd != STDIN_FILENO) { close(arch_in_fd); }
        if (create_members != NULL && *create_members != NULL)
        { create_archive(STDOUT_FILENO, create_members, verbose_flag); }
        return EXIT_SUCCESS;
    }

    // ;;;;;;;;; extract ONLY 
    if (extract_flag)
    {
        extract_archive(arch_in_fd, verbose_flag, extract_targets);
        if (arch_in_fd != STDIN_FILENO) { close(arch_in_fd); }
        return EXIT_SUCCESS;
    }

    // ;;;;;; validate ONLY 
    if (validate_flag)
    {
        validate_archive(arch_in_fd, verbose_flag);
        if (arch_in_fd != STDIN_FILENO) { close(arch_in_fd); }
        return EXIT_SUCCESS;
    }

    // ;;;;;;;;; toc only
    if (toc_flag)
    {
        toc_table(arch_in_fd, verbose_flag);
        if (arch_in_fd != STDIN_FILENO) { close(arch_in_fd); }
        return EXIT_SUCCESS;
    }
    fprintf(stderr, "nothing to do (try -c, -t, -x, or -V)\n");
    return EXIT_FAILURE;
}
// end of file...
