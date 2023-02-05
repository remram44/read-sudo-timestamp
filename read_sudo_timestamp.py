import ctypes
import os
import struct
import sys


CLOCK_BOOTTIME = 7


class timespec(ctypes.Structure):
    _fields_ = [
        ('tv_sec', ctypes.c_long),
        ('tv_nsec', ctypes.c_long),
    ]


librt = ctypes.CDLL('librt.so.1', use_errno=True)
clock_gettime = librt.clock_gettime
clock_gettime.argtypes = [ctypes.c_int, ctypes.POINTER(timespec)]


def boottime():
    t = timespec()
    if clock_gettime(CLOCK_BOOTTIME, ctypes.pointer(t)) != 0:
        err = ctypes.get_errno()
        raise OSError(err, os.strerror(err))

    return t.tv_sec + t.tv_nsec * 1e-9


target_tty = os.readlink('/proc/self/fd/0')
assert target_tty.startswith('/dev')
target_tty = os.stat(target_tty).st_rdev

target_uid = os.getuid()
target_ppid = os.getppid()
timeout = 900
boot = boottime()


with open('/tmp/ts', 'rb') as fp:  # /var/run/sudo/ts/remram
    ts = fp.read()

pos = 0
while pos < len(ts):
    version, = struct.unpack('=h', ts[0:2])
    size, = struct.unpack('=h', ts[2:4])

    if version != 2:
        print(f"skipping record with version {version}", file=sys.stderr)
        pos += size
        continue

    type_, flags, auth_uid, sid, start_time_sec, start_time_nsec, ts_sec, ts_nsec, u = struct.unpack(
        '='
        'hh' # unsigned short type, flags
        'l' # uid_t auth_uid
        'l' # pid_t sid
        'qq' # struct timespec start_time
        'qq' # struct timespec ts
        '8s' # union { dev_t, pid_t } u
        ,
        ts[pos + 4:pos + size],
    )
    pos += size

    if type_ == 4:
        # special lock record, skip
        continue
    assert type_ in (1, 2, 3)

    if flags & 0x01:  # disabled
        continue
    any_uid = flags & 0x02

    if not (any_uid or auth_uid == target_uid):
        print(f"record for other uid {auth_uid}")
        continue

    validity = ts_sec + ts_nsec * 1e-9 - boot + timeout

    if validity <= 0:
        continue

    if type_ == 1:
        print(f"found timestamp, valid for {validity:.2f}s")
    elif type_ == 2:
        tty, = struct.unpack('=q', u)
        tty_major = os.major(tty)
        tty_minor = os.minor(tty)
        if tty == target_tty:
            # TODO: Check session start_time
            print(f"found timestamp for tty {tty_major}:{tty_minor}, valid for {validity:.2f}s")
    elif type_ == 3:
        ppid, = struct.unpack('=l', u[:4])
        if ppid == target_ppid:
            # TODO: Check ppid start_time
            print(f"found timestamp for ppid {ppid}, valid for {validity:.2f}s")
