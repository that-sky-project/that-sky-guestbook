# `slots.bin` format

`slots.bin` is the account slot store used by That Sky Guestbook. It lives at:

```text
%APPDATA%\guest-login\slots.bin
```

All integers are little-endian.

## Header

```c
char  magic[4];   // "GSLT"
int32 version;    // 1
int32 slot_count;
int32 selected;   // -1 = Main (Steam), -2 = New guest, >= 0 = slot index
```

## Slot record (repeated `slot_count` times)

```c
char  name[32];   // NUL padded, UTF-8
uint8 valid;      // 1 = usable
uint8 auth[64];
```

## `auth[64]`

| Offset | Length | Meaning |
| --- | --- | --- |
| 0 | 16 | `user` UUID, raw bytes |
| 16 | 16 | `device` UUID, raw bytes |
| 32 | 32 | `key`, raw bytes |

The UUID bytes are the **plain UUID string bytes**, not Windows GUID mixed-endian.
Format them as:

```python
h = auth[0:16].hex()
uuid = f"{h[0:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"
```

Example: bytes `0123456789abcdef0123456789abcdef` become
`01234567-89ab-cdef-0123-456789abcdef`.

The game stores each account's data file as
`%APPDATA%\ThatGameCompany\com.tgc.sky.win\<user-uuid>.bin`.

## Legacy `auth.bin`

Older builds stored a single 64-byte blob at `%APPDATA%\guest-login\auth.bin`.
On first run, if `slots.bin` does not exist but `auth.bin` does, the mod imports
that blob as slot 0.
