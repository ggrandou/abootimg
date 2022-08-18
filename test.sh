#!/bin/bash

set -e

assert_has_file () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
    test -f "$1" || (echo 1>&2 "Couldn't find '$1' at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"; exit 1)
    } 3> /dev/null
}

assert_not_has_file () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
    if test -f "$1"; then
        sed -e 's/^/# /' < "$1" >&2
        echo 1>&2 "File '$1' exists at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"
        exit 1
    fi
    } 3> /dev/null
}

assert_files_equal () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
      cmp "$1" "$2" || (echo 1>&2 "Files $1 and $2 not equal at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"; exit 1)
    } 3> /dev/null
}

assert_file_has_content () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
    if ! grep -q -e "$2" "$1"; then
        sed -e 's/^/# /' < "$1" >&2
        echo 1>&2 "File '$1' doesn't match regexp '$2' at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"
        exit 1
    fi
    } 3> /dev/null
}

assert_not_file_has_content () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
    if grep -q -e "$2" "$1"; then
        sed -e 's/^/# /' < "$1" >&2
        echo 1>&2 "File '$1' incorrectly matches regexp '$2' at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"
        exit 1
    fi
    } 3> /dev/null
}

# Create 3k + some file with some char in
make_file () {
    a="$1$1$1$1$1$1$1$1$1$1"
    a="$a$a$a$a$a$a$a$a$a$a"
    a="$a$a$a$a$a$a$a$a$a$a"
    echo -n $a$a$a > $3
    for i in $(seq $2); do
        echo -n $1 >> $3
    done
}


SRCDIR=$(pwd)
VALGRIND=
if which valgrind &> /dev/null; then
    VALGRIND="valgrind -q --leak-check=no"
fi
ABOOTIMG="$VALGRIND $SRCDIR/abootimg"

TMPDIR=$(mktemp -d)
cd $TMPDIR

# Generate some data files
counter=1
for i in a b c d e f; do
    make_file $i $counter $i.img
    counter=$((counter+1))
done

# Test content - version 0

$ABOOTIMG --create test.img -k a.img -r b.img -s c.img \
          -c "pagesize = 4096" \
          -c "cmdline = CMDLINE" \
          -c "name = NAME" \
          -c "kerneladdr = 0x10000001" \
          -c "ramdiskaddr = 0x10000002" \
          -c "secondaddr = 0x10000003" \
          -c "tagsaddr = 0x10000004" \
          > /dev/null
assert_has_file test.img

$ABOOTIMG -i test.img > test.info
assert_file_has_content test.info "version *= 0"
assert_file_has_content test.info "cmdline *= CMDLINE"
assert_file_has_content test.info "page size *= 4096"
assert_file_has_content test.info "kernel size *= 3001 "
assert_file_has_content test.info "ramdisk size *= 3002 "
assert_file_has_content test.info "second stage size *= 3003 "
assert_not_file_has_content test.info "recovery dtbo size "
assert_not_file_has_content test.info "dtb size"
assert_file_has_content test.info "kernel: *0x10000001"
assert_file_has_content test.info "ramdisk: *0x10000002"
assert_file_has_content test.info "second stage: *0x10000003"
assert_file_has_content test.info "tags: *0x10000004"

$ABOOTIMG -x test.img test.cfg test.kernel test.ramdisk test.secondstage test.dtb test.recoverydtbo > /dev/null
assert_files_equal test.kernel a.img
assert_files_equal test.ramdisk b.img
assert_files_equal test.secondstage c.img
assert_not_has_file test.recoverydtbo
assert_not_has_file test.dtb

rm test.*

# Test content - version 1 (adds recovery dtbo)

$ABOOTIMG --create test.img -k a.img -r b.img -s c.img -o d.img \
          -c "recoverydtobooffs = 0x10000005" \
           > /dev/null
assert_has_file test.img

$ABOOTIMG -i test.img > test.info
assert_file_has_content test.info "version *= 1"
assert_file_has_content test.info "kernel size *= 3001 "
assert_file_has_content test.info "ramdisk size *= 3002 "
assert_file_has_content test.info "second stage size *= 3003 "
assert_file_has_content test.info "recovery dtbo size.*= 3004 "
assert_not_file_has_content test.info "dtb size"
assert_file_has_content test.info "recovery dtbo: *0x10000005"

$ABOOTIMG -x test.img test.cfg test.kernel test.ramdisk test.secondstage test.dtb test.recoverydtbo > /dev/null
assert_files_equal test.kernel a.img
assert_files_equal test.ramdisk b.img
assert_files_equal test.secondstage c.img
assert_files_equal test.recoverydtbo d.img
assert_not_has_file test.dtb

rm test.*

# Test content - version 2 (adds dtb)

$ABOOTIMG --create test.img -k a.img -r b.img -s c.img -o d.img -d e.img \
          -c "recoverydtobooffs = 0x10000005" \
          -c "dtbaddr = 0x10000006" \
            > /dev/null
assert_has_file test.img

$ABOOTIMG -i test.img > test.info
assert_file_has_content test.info "version *= 2"
assert_file_has_content test.info "kernel size *= 3001 "
assert_file_has_content test.info "ramdisk size *= 3002 "
assert_file_has_content test.info "second stage size *= 3003 "
assert_file_has_content test.info "recovery dtbo size.*= 3004 "
assert_file_has_content test.info "dtb size *= 3005 "
assert_file_has_content test.info "dtb: *0x10000006"

$ABOOTIMG -x test.img test.cfg test.kernel test.ramdisk test.secondstage test.dtb test.recoverydtbo > /dev/null
assert_files_equal test.kernel a.img
assert_files_equal test.ramdisk b.img
assert_files_equal test.secondstage c.img
assert_files_equal test.recoverydtbo d.img
assert_files_equal test.dtb e.img

rm test.*

rm -rf $TMPDIR
