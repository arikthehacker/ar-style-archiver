# ar-style-archiver

![language](https://img.shields.io/badge/language-C-blue) ![platform](https://img.shields.io/badge/platform-Linux-lightgrey)
<!-- ![CI](https://github.com/arikthehacker/ar-style-archiver/actions/workflows/ci.yml/badge.svg) -->

`arvik` is an `ar`-style archive tool: it packs several files into one archive and
unpacks them again, keeping each file's name, timestamp, owner, group, mode, and size.
Every member also carries MD4 checksums of its header and data, which the tool can
validate on read.

## quickstart

```
git clone https://github.com/arikthehacker/ar-style-archiver.git
cd ar-style-archiver
sudo apt-get install -y build-essential libmd-dev
make run
```

expected output:

```
./arvik-md4 -cvf demo.arvik one.txt two.txt
added one.txt
added two.txt
./arvik-md4 -tvf demo.arvik
file name: one.txt
    mode:       rw-r--r--
    uid:                0  root
    gid:                0  root
    size:                4  bytes
    mtime:      Oct  6 16:47 2026
    header md4: 3745c82e58def47f1b301307f0453a82
    data md4:   fe51b9cbeccc0bfc834511106051c636
file name: two.txt
    ...
```

The owner, mtime, and md4 values depend on the run (the header checksum covers the
timestamp), so they change each time.

## how it works

A member header is a fixed-size block of character fields, written and read as one block:

```c
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
```

On create, the tool fills these fields from `stat`, writes the file data, then writes a
footer with the MD4 of the header and the MD4 of the data. On `-V` it recomputes both
while reading and compares them to the stored values:

```c
MD4Update(&ctx, (const unsigned char *)&md, sizeof(arvik_header_t));
...
header_ok = (strncasecmp(hex_hdr, mf.md4sum_header, MD4_DIGEST_LENGTH * 2) == 0);
data_ok   = (strncasecmp(hex_dat, mf.md4sum_data, MD4_DIGEST_LENGTH * 2) == 0);
```

It uses raw `open`/`read`/`write`/`lseek` throughout, and links `libmd` (`-lmd`) for MD4.

## options

Running `./arvik-md4 -h`:

```
Usage: arvik-md4 -[cxtvVf:h] archive-file file...
        -c           create a new archive file
        -x           extract members from an existing archive file
        -t           show the table of contents of archive file
        -f filename  name of archive file to use
        -V           Validate the md4 values for the header and data
        -v           verbose output
        -h           show help text
```
