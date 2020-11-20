/*
 * wildmidi.c -- Midi Player using the WildMidi Midi Processing Library
 *
 * Copyright (C) WildMidi Developers 2001-2016
 *
 * This file is part of WildMIDI.
 *
 * WildMIDI is free software: you can redistribute and/or modify the player
 * under the terms of the GNU General Public License and you can redistribute
 * and/or modify the library under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the licenses, or(at your option) any later version.
 *
 * WildMIDI is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License and
 * the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License and the
 * GNU Lesser General Public License along with WildMIDI.  If not,  see
 * <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "wildplay.h"

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "out_noout.h"
#include "out_ahi.h"
#include "out_alsa.h"
#include "out_dart.h"
#include "out_dossb.h"
#include "out_openal.h"
#include "out_oss.h"
#include "out_wave.h"
#include "out_win32mm.h"

// available outputs
wildmidi_info available_outputs[TOTAL_OUT] = {
    {
        "noout",
        "No output",
        AUDIODRV_NONE,
        open_output_noout,
        send_output_noout,
        close_output_noout,
        pause_output_noout,
        resume_output_noout
    },
    {
        "wave",
        "Save stream to WAVE file",
        AUDIODRV_WAVE,
        open_wav_output,
        write_wav_output,
        close_wav_output,
        pause_output_noout,
        resume_output_noout
    },
    {
        "alsa",
        "Advanced Linux Sound Architecture (ALSA) output",
        AUDIODRV_ALSA,
        open_alsa_output,
        write_alsa_output,
        close_alsa_output,
        pause_output_noout,
        resume_output_noout
    },
    {
        "oss",
        "Open Sound System (OSS) output",
        AUDIODRV_OSS,
        open_oss_output,
        write_oss_output,
        close_oss_output,
        pause_oss_output,
        resume_output_noout
    },
    {
        "openal",
        "OpenAL output",
        AUDIODRV_OPENAL,
        open_openal_output,
        write_openal_output,
        close_openal_output,
        pause_output_openal,
        resume_output_noout
    },
    {
        "ahi",
        "Amiga AHI output",
        AUDIODRV_AHI,
        open_ahi_output,
        write_ahi_output,
        close_ahi_output,
        pause_output_noout,
        resume_output_noout
    },
    {
        "win32mm",
        "Windows MM output",
        AUDIODRV_WIN32_MM,
        open_mm_output,
        write_mm_output,
        close_mm_output,
        pause_output_noout,
        resume_output_noout
    },
    {
        "os2dart",
        "OS/2 DART output",
        AUDIODRV_OS2DART,
        open_dart_output,
        write_dart_output,
        close_dart_output,
        pause_output_noout,
        resume_output_noout
    },
    {
        "dossb",
        "DOS SoundBlaster output",
        AUDIODRV_DOSSB,
        open_sb_output,
        write_sb_s16stereo,   // FIXME
        close_sb_output,
        sb_silence_s16,       // FIXME
        resume_output_noout
    },
};

#if defined(__DJGPP__)
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "getopt_long.h"
#include <conio.h>
#define getopt dj_getopt /* hack */
#include <unistd.h>
#undef getopt
#define msleep(s) usleep((s)*1000)
#include <io.h>
#include <dir.h>
#ifdef AUDIODRV_DOSSB
#include "dossb.h"
#endif

#elif (defined _WIN32) || (defined __CYGWIN__)
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <conio.h>
#include <windows.h>
#include <mmsystem.h>
#define msleep(s) Sleep((s))
#include <io.h>
#include "getopt_long.h"
#ifdef __WATCOMC__
#define _putch putch
#endif

#elif defined(__OS2__) || defined(__EMX__)
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_OS2MM
#ifdef __EMX__
#define INCL_KBD
#define INCL_VIO
#endif
#include <os2.h>
#include <os2me.h>
#include <conio.h>
#define msleep(s) DosSleep((s))
#include <fcntl.h>
#include <io.h>
#include "getopt_long.h"
#ifdef __EMX__
#include <sys/types.h> /* for off_t typedef */
int putch (int c) {
    char ch = c;
    VioWrtTTY(&ch, 1, 0);
    return c;
}
int kbhit (void) {
    KBDKEYINFO k;
    if (KbdPeek(&k, 0))
        return 0;
    return (k.fbStatus & KBDTRF_FINAL_CHAR_IN);
}
#endif

#elif defined(WILDMIDI_AMIGA)
extern void amiga_sysinit (void);
extern int amiga_usleep(unsigned long millisec);
#define msleep(s) amiga_usleep((s)*1000)
extern int amiga_getch (unsigned char *ch);
#include <proto/exec.h>
#include <proto/dos.h>
#include "getopt_long.h"
#ifdef AUDIODRV_AHI
#include <devices/ahi.h>
#ifdef __amigaos4__
#define SHAREDMEMFLAG MEMF_SHARED
#else
#define SHAREDMEMFLAG MEMF_PUBLIC
#endif
#endif

#else /* unix build */
static int msleep(unsigned long millisec);
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#endif /* !_WIN32, !__DJGPP__ (unix build) */

#include "filenames.h"
#include "wm_tty.h"
#include "wildmidi_lib.h"

struct _midi_test {
    uint8_t *data;
    uint32_t size;
};

/* scale test from 0 to 127
 * test a
 * offset 18-21 (0x12-0x15) - track size
 * offset 25 (0x1A) = bank number
 * offset 28 (0x1D) = patch number
 */
static uint8_t midi_test_c_scale[] = {
    0x4d, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, /* 0x00    */
    0x00, 0x00, 0x00, 0x01, 0x00, 0x06, 0x4d, 0x54, /* 0x08    */
    0x72, 0x6b, 0x00, 0x00, 0x02, 0x63, 0x00, 0xb0, /* 0x10    */
    0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x90, 0x00, /* 0x18  C */
    0x64, 0x08, 0x80, 0x00, 0x00, 0x08, 0x90, 0x02, /* 0x20  D */
    0x64, 0x08, 0x80, 0x02, 0x00, 0x08, 0x90, 0x04, /* 0x28  E */
    0x64, 0x08, 0x80, 0x04, 0x00, 0x08, 0x90, 0x05, /* 0x30  F */
    0x64, 0x08, 0x80, 0x05, 0x00, 0x08, 0x90, 0x07, /* 0x38  G */
    0x64, 0x08, 0x80, 0x07, 0x00, 0x08, 0x90, 0x09, /* 0x40  A */
    0x64, 0x08, 0x80, 0x09, 0x00, 0x08, 0x90, 0x0b, /* 0x48  B */
    0x64, 0x08, 0x80, 0x0b, 0x00, 0x08, 0x90, 0x0c, /* 0x50  C */
    0x64, 0x08, 0x80, 0x0c, 0x00, 0x08, 0x90, 0x0e, /* 0x58  D */
    0x64, 0x08, 0x80, 0x0e, 0x00, 0x08, 0x90, 0x10, /* 0x60  E */
    0x64, 0x08, 0x80, 0x10, 0x00, 0x08, 0x90, 0x11, /* 0x68  F */
    0x64, 0x08, 0x80, 0x11, 0x00, 0x08, 0x90, 0x13, /* 0x70  G */
    0x64, 0x08, 0x80, 0x13, 0x00, 0x08, 0x90, 0x15, /* 0x78  A */
    0x64, 0x08, 0x80, 0x15, 0x00, 0x08, 0x90, 0x17, /* 0x80  B */
    0x64, 0x08, 0x80, 0x17, 0x00, 0x08, 0x90, 0x18, /* 0x88  C */
    0x64, 0x08, 0x80, 0x18, 0x00, 0x08, 0x90, 0x1a, /* 0x90  D */
    0x64, 0x08, 0x80, 0x1a, 0x00, 0x08, 0x90, 0x1c, /* 0x98  E */
    0x64, 0x08, 0x80, 0x1c, 0x00, 0x08, 0x90, 0x1d, /* 0xA0  F */
    0x64, 0x08, 0x80, 0x1d, 0x00, 0x08, 0x90, 0x1f, /* 0xA8  G */
    0x64, 0x08, 0x80, 0x1f, 0x00, 0x08, 0x90, 0x21, /* 0xB0  A */
    0x64, 0x08, 0x80, 0x21, 0x00, 0x08, 0x90, 0x23, /* 0xB8  B */
    0x64, 0x08, 0x80, 0x23, 0x00, 0x08, 0x90, 0x24, /* 0xC0  C */
    0x64, 0x08, 0x80, 0x24, 0x00, 0x08, 0x90, 0x26, /* 0xC8  D */
    0x64, 0x08, 0x80, 0x26, 0x00, 0x08, 0x90, 0x28, /* 0xD0  E */
    0x64, 0x08, 0x80, 0x28, 0x00, 0x08, 0x90, 0x29, /* 0xD8  F */
    0x64, 0x08, 0x80, 0x29, 0x00, 0x08, 0x90, 0x2b, /* 0xE0  G */
    0x64, 0x08, 0x80, 0x2b, 0x00, 0x08, 0x90, 0x2d, /* 0xE8  A */
    0x64, 0x08, 0x80, 0x2d, 0x00, 0x08, 0x90, 0x2f, /* 0xF0  B */
    0x64, 0x08, 0x80, 0x2f, 0x00, 0x08, 0x90, 0x30, /* 0xF8  C */
    0x64, 0x08, 0x80, 0x30, 0x00, 0x08, 0x90, 0x32, /* 0x100 D */
    0x64, 0x08, 0x80, 0x32, 0x00, 0x08, 0x90, 0x34, /* 0x108 E */
    0x64, 0x08, 0x80, 0x34, 0x00, 0x08, 0x90, 0x35, /* 0x110 F */
    0x64, 0x08, 0x80, 0x35, 0x00, 0x08, 0x90, 0x37, /* 0x118 G */
    0x64, 0x08, 0x80, 0x37, 0x00, 0x08, 0x90, 0x39, /* 0x120 A */
    0x64, 0x08, 0x80, 0x39, 0x00, 0x08, 0x90, 0x3b, /* 0X128 B */
    0x64, 0x08, 0x80, 0x3b, 0x00, 0x08, 0x90, 0x3c, /* 0x130 C */
    0x64, 0x08, 0x80, 0x3c, 0x00, 0x08, 0x90, 0x3e, /* 0x138 D */
    0x64, 0x08, 0x80, 0x3e, 0x00, 0x08, 0x90, 0x40, /* 0X140 E */
    0x64, 0x08, 0x80, 0x40, 0x00, 0x08, 0x90, 0x41, /* 0x148 F */
    0x64, 0x08, 0x80, 0x41, 0x00, 0x08, 0x90, 0x43, /* 0x150 G */
    0x64, 0x08, 0x80, 0x43, 0x00, 0x08, 0x90, 0x45, /* 0x158 A */
    0x64, 0x08, 0x80, 0x45, 0x00, 0x08, 0x90, 0x47, /* 0x160 B */
    0x64, 0x08, 0x80, 0x47, 0x00, 0x08, 0x90, 0x48, /* 0x168 C */
    0x64, 0x08, 0x80, 0x48, 0x00, 0x08, 0x90, 0x4a, /* 0x170 D */
    0x64, 0x08, 0x80, 0x4a, 0x00, 0x08, 0x90, 0x4c, /* 0x178 E */
    0x64, 0x08, 0x80, 0x4c, 0x00, 0x08, 0x90, 0x4d, /* 0x180 F */
    0x64, 0x08, 0x80, 0x4d, 0x00, 0x08, 0x90, 0x4f, /* 0x188 G */
    0x64, 0x08, 0x80, 0x4f, 0x00, 0x08, 0x90, 0x51, /* 0x190 A */
    0x64, 0x08, 0x80, 0x51, 0x00, 0x08, 0x90, 0x53, /* 0x198 B */
    0x64, 0x08, 0x80, 0x53, 0x00, 0x08, 0x90, 0x54, /* 0x1A0 C */
    0x64, 0x08, 0x80, 0x54, 0x00, 0x08, 0x90, 0x56, /* 0x1A8 D */
    0x64, 0x08, 0x80, 0x56, 0x00, 0x08, 0x90, 0x58, /* 0x1B0 E */
    0x64, 0x08, 0x80, 0x58, 0x00, 0x08, 0x90, 0x59, /* 0x1B8 F */
    0x64, 0x08, 0x80, 0x59, 0x00, 0x08, 0x90, 0x5b, /* 0x1C0 G */
    0x64, 0x08, 0x80, 0x5b, 0x00, 0x08, 0x90, 0x5d, /* 0x1C8 A */
    0x64, 0x08, 0x80, 0x5d, 0x00, 0x08, 0x90, 0x5f, /* 0x1D0 B */
    0x64, 0x08, 0x80, 0x5f, 0x00, 0x08, 0x90, 0x60, /* 0x1D8 C */
    0x64, 0x08, 0x80, 0x60, 0x00, 0x08, 0x90, 0x62, /* 0x1E0 D */
    0x64, 0x08, 0x80, 0x62, 0x00, 0x08, 0x90, 0x64, /* 0x1E8 E */
    0x64, 0x08, 0x80, 0x64, 0x00, 0x08, 0x90, 0x65, /* 0x1F0 F */
    0x64, 0x08, 0x80, 0x65, 0x00, 0x08, 0x90, 0x67, /* 0x1F8 G */
    0x64, 0x08, 0x80, 0x67, 0x00, 0x08, 0x90, 0x69, /* 0x200 A */
    0x64, 0x08, 0x80, 0x69, 0x00, 0x08, 0x90, 0x6b, /* 0x208 B */
    0x64, 0x08, 0x80, 0x6b, 0x00, 0x08, 0x90, 0x6c, /* 0x210 C */
    0x64, 0x08, 0x80, 0x6c, 0x00, 0x08, 0x90, 0x6e, /* 0x218 D */
    0x64, 0x08, 0x80, 0x6e, 0x00, 0x08, 0x90, 0x70, /* 0x220 E */
    0x64, 0x08, 0x80, 0x70, 0x00, 0x08, 0x90, 0x71, /* 0x228 F */
    0x64, 0x08, 0x80, 0x71, 0x00, 0x08, 0x90, 0x73, /* 0x230 G */
    0x64, 0x08, 0x80, 0x73, 0x00, 0x08, 0x90, 0x75, /* 0x238 A */
    0x64, 0x08, 0x80, 0x75, 0x00, 0x08, 0x90, 0x77, /* 0x240 B */
    0x64, 0x08, 0x80, 0x77, 0x00, 0x08, 0x90, 0x78, /* 0x248 C */
    0x64, 0x08, 0x80, 0x78, 0x00, 0x08, 0x90, 0x7a, /* 0x250 D */
    0x64, 0x08, 0x80, 0x7a, 0x00, 0x08, 0x90, 0x7c, /* 0x258 E */
    0x64, 0x08, 0x80, 0x7c, 0x00, 0x08, 0x90, 0x7d, /* 0x260 F */
    0x64, 0x08, 0x80, 0x7d, 0x00, 0x08, 0x90, 0x7f, /* 0x268 G */
    0x64, 0x08, 0x80, 0x7f, 0x00, 0x08, 0xff, 0x2f, /* 0x270   */
    0x00                                            /* 0x278   */
};

static struct _midi_test midi_test[] = {
    { midi_test_c_scale, 663 },
    { NULL, 0 }
};

static int midi_test_max = 1;

/*
 ==============================
 Audio Output Functions

 We have two 'drivers': first is the wav file writer which is
 always available. the second, if it is really compiled in,
 is the system audio output driver. only _one of the two_ can
 be active, not both.
 ==============================
 */

unsigned int rate = 32072;

#define wmidi_geterrno() errno /* generic case */
#if defined(_WIN32)
int audio_fd = -1;
#define WM_IS_BADF(_fd) ((_fd)<0)
#define WM_BADF (-1)
int wmidi_fileexists (const char *path) {
    return (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES);
}
int wmidi_open_write (const char *path) {
    return _open(path, (O_RDWR | O_CREAT | O_TRUNC | O_BINARY), 0664);
}
void wmidi_close (int fd) {
    _close(fd);
}
long wmidi_seekset (int fd, long ofs) {
    return _lseek(fd, ofs, SEEK_SET);
}
int wmidi_write (int fd, const void *buf, size_t size) {
    return _write(fd, buf, size);
}

#elif defined(__DJGPP__)
int audio_fd = -1;
#define WM_IS_BADF(_fd) ((_fd)<0)
#define WM_BADF -1
static inline int wmidi_fileexists (const char *path) {
    struct ffblk f;
    return (findfirst(path, &f, FA_ARCH | FA_RDONLY) == 0);
}
static inline int wmidi_open_write (const char *path) {
    return open(path, (O_RDWR | O_CREAT | O_TRUNC | O_BINARY), 0664);
}
static inline void wmidi_close (int fd) {
    close(fd);
}
static inline off_t wmidi_seekset (int fd, off_t ofs) {
    return lseek(fd, ofs, SEEK_SET);
}
static inline int wmidi_write (int fd, const void *buf, size_t size) {
    return write(fd, buf, size);
}

#elif defined(__OS2__) || defined(__EMX__)
int audio_fd = -1;
#define WM_IS_BADF(_fd) ((_fd)<0)
#define WM_BADF -1
static inline int wmidi_fileexists (const char *path) {
    int f = open(path, (O_RDONLY | O_BINARY));
    if (f != -1) { close(f); return 1; } else return 0;
}
static inline int wmidi_open_write (const char *path) {
    return open(path, (O_RDWR | O_CREAT | O_TRUNC | O_BINARY), 0664);
}
static inline void wmidi_close (int fd) {
    close(fd);
}
static inline off_t wmidi_seekset (int fd, off_t ofs) {
    return lseek(fd, ofs, SEEK_SET);
}
static inline int wmidi_write (int fd, const void *buf, size_t size) {
    return write(fd, buf, size);
}

#elif defined(WILDMIDI_AMIGA)
BPTR audio_fd = 0;
#define WM_IS_BADF(_fd) ((_fd)==0)
#define WM_BADF 0
#undef wmidi_geterrno
static int wmidi_geterrno (void) {
    switch (IoErr()) {
    case ERROR_OBJECT_NOT_FOUND: return ENOENT;
    case ERROR_DISK_FULL: return ENOSPC;
    }
    return EIO; /* better ?? */
}
static inline int wmidi_fileexists (const char *path) {
    BPTR fd = Open((const STRPTR)path, MODE_OLDFILE);
    if (!fd) return 0;
    Close(fd); return 1;
}
static inline BPTR wmidi_open_write (const char *path) {
    return Open((const STRPTR) path, MODE_NEWFILE);
}
static inline LONG wmidi_close (BPTR fd) {
    return Close(fd);
}
static inline LONG wmidi_seekset (BPTR fd, LONG ofs) {
    return Seek(fd, ofs, OFFSET_BEGINNING);
}
static LONG wmidi_write (BPTR fd, /*const*/ void *buf, LONG size) {
    LONG written = 0, result;
    unsigned char *p = (unsigned char *)buf;
    while (written < size) {
        result = Write(fd, p + written, size - written);
        if (result < 0) return result;
        written += result;
    }
    return written;
}

#else /* common posix case */
int audio_fd = -1;
#define WM_IS_BADF(_fd) ((_fd)<0)
#define WM_BADF (-1)
int wmidi_fileexists (const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}
int wmidi_open_write (const char *path) {
    return open(path, (O_RDWR | O_CREAT | O_TRUNC), (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH));
}
int wmidi_close (int fd) {
    return close(fd);
}
off_t wmidi_seekset (int fd, off_t ofs) {
    return lseek(fd, ofs, SEEK_SET);
}
ssize_t wmidi_write (int fd, const void *buf, size_t size) {
    return write(fd, buf, size);
}
#endif

/*
 MIDI Output Functions
 */
static char midi_file[1024];

static void mk_midifile_name(const char *src) {
    char *p;
    int len;

    strncpy(midi_file, src, sizeof(midi_file) - 1);
    midi_file[sizeof(midi_file) - 1] = 0;

    p = strrchr(midi_file, '.');
    if (p && (len = strlen(p)) <= 4) {
        if (p - midi_file <= (int)sizeof(midi_file) - 5) {
            memcpy(p, ".mid", 5);
            return;
        }
    }

    len = strlen(midi_file);
    if (len > (int)sizeof(midi_file) - 5)
        len = (int)sizeof(midi_file) - 5;
    p = &midi_file[len];
    memcpy(p, ".mid", 5);
}

static int write_midi_output(void *output_data, int output_size) {
    if (midi_file[0] == '\0')
        return (-1);

/*
 * Test if file already exists 
 */
    if (wmidi_fileexists(midi_file)) {
        fprintf(stderr, "\rError: %s already exists\r\n", midi_file);
        return (-1);
    }

    audio_fd = wmidi_open_write(midi_file);
    if (WM_IS_BADF(audio_fd)) {
        fprintf(stderr, "Error: unable to open file for writing (%s)\r\n", strerror(wmidi_geterrno()));
        return (-1);
    }

    if (wmidi_write(audio_fd, output_data, output_size) < 0) {
        fprintf(stderr, "\nERROR: failed writing midi (%s)\r\n", strerror(wmidi_geterrno()));
        wmidi_close(audio_fd);
        audio_fd = WM_BADF;
        return (-1);
    }

    wmidi_close(audio_fd);
    audio_fd = WM_BADF;
    return (0);
}

/*
 Wav Output Functions
 */

// FIXME get rid of this
char wav_file[1024];

#if ((defined _WIN32) || (defined __CYGWIN__)) && (AUDIODRV_WIN32_MM == 1)

#elif (defined(__OS2__) || defined(__EMX__)) && (AUDIODRV_OS2DART == 1)

#elif defined(__DJGPP__) && (AUDIODRV_DOSSB == 1)

#elif defined(WILDMIDI_AMIGA) && (AUDIODRV_AHI == 1)

#else
#if (AUDIODRV_ALSA == 1)
// FIXME get rid of this
char pcmname[64];
#elif AUDIODRV_OSS == 1
// FIXME get rid of this
char pcmname[64];
#elif AUDIODRV_OPENAL == 1

#else /* no audio output driver compiled in: */

#endif /* AUDIODRV_ALSA */
#endif /* _WIN32 || __CYGWIN__ */

static struct option const long_options[] = {
    { "version", 0, 0, 'v' },
    { "help", 0, 0, 'h' },
    { "playback", 1, 0, 'P'},
    { "rate", 1, 0, 'r' },
    { "mastervol", 1, 0, 'm' },
    { "config", 1, 0, 'c' },
    { "wavout", 1, 0, 'o' },
    { "tomidi", 1, 0, 'x' },
    { "convert", 1, 0, 'g' },
    { "frequency", 1, 0, 'f' },
    { "log_vol", 0, 0, 'l' },
    { "reverb", 0, 0, 'b' },
    { "test_midi", 0, 0, 't' },
    { "test_bank", 1, 0, 'k' },
    { "test_patch", 1, 0, 'p' },
    { "enhanced", 0, 0, 'e' },
#if AUDIODRV_OSS == 1 || AUDIODRV_ALSA == 1
    { "device", 1, 0, 'd' },
#endif
    { "roundtempo", 0, 0, 'n' },
    { "skipsilentstart", 0, 0, 's' },
    { "textaslyric", 0, 0, 'a' },
    { "playfrom", 1, 0, 'i'},
    { "playto", 1, 0, 'j'},
    { NULL, 0, NULL, 0 }
};

static void do_help(void) {
    printf("  -v    --version     Display version info and exit\n");
    printf("  -h    --help        Display this help and exit\n");
#if AUDIODRV_OSS == 1 || AUDIODRV_ALSA == 1
    printf("  -d D  --device=D    Use device D for audio output instead of default\n");
#endif
    printf("MIDI Options:\n");
    printf("  -n    --roundtempo  Round tempo to nearest whole number\n");
    printf("  -s    --skipsilentstart Skips any silence at the start of playback\n");
    printf("  -t    --test_midi   Listen to test MIDI\n");
    printf("Non-MIDI Options:\n");
    printf("  -P P  --playback=P  Set P as playback output.\n");
    printf("  -x    --tomidi      Convert file to midi and save to file\n");
    printf("  -g    --convert     Convert XMI: 0 - No Conversion (default)\n");
    printf("                                   1 - MT32 to GM\n");
    printf("                                   2 - MT32 to GS\n");
    printf("  -f F  --frequency=F Use frequency F Hz for playback (MUS)\n");
    printf("Software Wavetable Options:\n");
    printf("  -o W  --wavout=W    Save output to W in 16bit stereo format wav file\n");
    printf("  -l    --log_vol     Use log volume adjustments\n");
    printf("  -r N  --rate=N      Set sample rate to N samples per second (Hz)\n");
    printf("  -c P  --config=P    Point to your wildmidi.cfg config file name/path\n");
    printf("                      defaults to: %s\n", WILDMIDI_CFG);
    printf("  -m V  --mastervol=V Set the master volume (0..127), default is 100\n");
    printf("  -b    --reverb      Enable final output reverb engine\n\n");
}

static void do_available_outputs(void) {
    printf("Available playback outputs (option -P):\n");
    for (int i = 0 ; i < TOTAL_OUT; i++) {
        if (available_outputs[i].enabled == 1) {
            printf("  %-20s%s\n",
                 available_outputs[i].name, available_outputs[i].description);
        }
    }
}

static void do_version(void) {
    printf("\nWildMidi %s Open Source Midi Sequencer\n", PACKAGE_VERSION);
    printf("Copyright (C) WildMIDI Developers 2001-2016\n\n");
    printf("WildMidi comes with ABSOLUTELY NO WARRANTY\n");
    printf("This is free software, and you are welcome to redistribute it under\n");
    printf("the terms and conditions of the GNU General Public License version 3.\n");
    printf("For more information see COPYING\n\n");
    printf("Report bugs to %s\n", PACKAGE_BUGREPORT);
    printf("WildMIDI homepage is at %s\n\n", PACKAGE_URL);
}

static void do_syntax(void) {
    printf("Usage: wildmidi [options] filename.mid\n\n");
}

static char config_file[1024];

int main(int argc, char **argv) {
    struct _WM_Info *wm_info;
    int i, res;
    int playback_id = NO_OUT;
    int option_index = 0;
    uint16_t mixer_options = 0;
    void *midi_ptr;
    uint8_t master_volume = 100;
    int8_t *output_buffer;
    uint32_t perc_play;
    uint32_t pro_mins;
    uint32_t pro_secs;
    uint32_t apr_mins;
    uint32_t apr_secs;
    char modes[5];
    uint8_t ch;
    int test_midi = 0;
    int test_count = 0;
    uint8_t *test_data;
    uint8_t test_bank = 0;
    uint8_t test_patch = 0;
    static char spinner[] = "|/-\\";
    static int spinpoint = 0;
    unsigned long int seek_to_sample;
    uint32_t samples = 0;
    int inpause = 0;
    char * ret_err = NULL;
    long libraryver;
    char * lyric = NULL;
    char *last_lyric = NULL;
    size_t last_lyric_length = 0;
    int8_t kareoke = 0;
#define MAX_LYRIC_CHAR 128
    char lyrics[MAX_LYRIC_CHAR + 1];
#define MAX_DISPLAY_LYRICS 29
    char display_lyrics[MAX_DISPLAY_LYRICS + 1];

    unsigned long int play_from = 0;
    unsigned long int play_to = 0;

    memset(lyrics,' ',MAX_LYRIC_CHAR);
    memset(display_lyrics,' ',MAX_DISPLAY_LYRICS);

#if (AUDIODRV_OSS == 1) || (AUDIODRV_ALSA == 1)
    pcmname[0] = 0;
#endif
    config_file[0] = 0;
    wav_file[0] = 0;
    midi_file[0] = 0;

    do_version();
    while (1) {
        i = getopt_long(argc, argv, "0vho:tx:g:P:f:lr:c:m:btak:p:ed:nsi:j:", long_options,
                &option_index);
        if (i == -1)
            break;
        switch (i) {
        case 'v': /* Version */
            return (0);
        case 'h': /* help */
            do_syntax();
            do_help();
            do_available_outputs();
            return (0);
        case 'P': /* Playback */
            if (!*optarg) {
                fprintf(stderr, "Error: empty playback name.\n");
                return (1);
            } else {
                for (i = 0; i < TOTAL_OUT; i++) {
                    if (strcmp(available_outputs[i].name, optarg) == 0) {
                        playback_id = i;
                        break;
                    }
                }
            }
            if (playback_id == NO_OUT) {
                fprintf(stderr, "Error: chosen playback %s is not available.\n", optarg);
                return (1);
            }
            break;
        case 'r': /* Sample Rate */
            res = atoi(optarg);
            if (res < 0 || res > 65535) {
                fprintf(stderr, "Error: bad rate %i.\n", res);
                return (1);
            }
            rate = (uint32_t) res;
            break;
        case 'b': /* Reverb */
            mixer_options |= WM_MO_REVERB;
            break;
        case 'm': /* Master Volume */
            master_volume = (uint8_t) atoi(optarg);
            break;
        case 'o': /* Wav Output */
            if (!*optarg) {
                fprintf(stderr, "Error: empty wavfile name.\n");
                return (1);
            }
            strncpy(wav_file, optarg, sizeof(wav_file));
            wav_file[sizeof(wav_file) - 1] = 0;
            break;
        case 'g': /* XMIDI Conversion */
            WildMidi_SetCvtOption(WM_CO_XMI_TYPE, (uint16_t) atoi(optarg));
            break;
        case 'f': /* MIDI-like Conversion */
            WildMidi_SetCvtOption(WM_CO_FREQUENCY, (uint16_t) atoi(optarg));
            break;
        case 'x': /* MIDI Output */
            if (!*optarg) {
                fprintf(stderr, "Error: empty midi name.\n");
                return (1);
            }
            strncpy(midi_file, optarg, sizeof(midi_file));
            midi_file[sizeof(midi_file) - 1] = 0;
            break;
        case 'c': /* Config File */
            if (!*optarg) {
                fprintf(stderr, "Error: empty config name.\n");
                return (1);
            }
            strncpy(config_file, optarg, sizeof(config_file));
            config_file[sizeof(config_file) - 1] = 0;
            break;
#if (AUDIODRV_OSS == 1) || (AUDIODRV_ALSA == 1)
        case 'd': /* Output device */
            if (!*optarg) {
                fprintf(stderr, "Error: empty device name.\n");
                return (1);
            }
            strncpy(pcmname, optarg, sizeof(pcmname));
            pcmname[sizeof(pcmname) - 1] = 0;
            break;
#endif
        case 'e': /* Enhanced Resampling */
            mixer_options |= WM_MO_ENHANCED_RESAMPLING;
            break;
        case 'l': /* log volume */
            mixer_options |= WM_MO_LOG_VOLUME;
            break;
        case 't': /* play test midis */
            test_midi = 1;
            break;
        case 'k': /* set test bank */
            test_bank = (uint8_t) atoi(optarg);
            break;
        case 'p': /* set test patch */
            test_patch = (uint8_t) atoi(optarg);
            break;
        case 'n': /* whole number tempo */
            mixer_options |= WM_MO_ROUNDTEMPO;
            break;
        case 'a':
            /* Some files have the lyrics in the text meta event.
             * This option reads lyrics from there instead.  */
            mixer_options |= WM_MO_TEXTASLYRIC;
            break;
        case 's': /* strip silence at start */
            mixer_options |= WM_MO_STRIPSILENCE;
            break;
        case '0': /* treat as type 2 midi when writing to file */
            mixer_options |= WM_MO_SAVEASTYPE0;
            break;
        case 'i':
            play_from = (unsigned long int)(atof(optarg) * (double)rate);
            break;
        case 'j':
            play_to = (unsigned long int)(atof(optarg) * (double)rate);
            break;
        default:
            do_syntax();
            return (1);
        }
    }

    if (optind >= argc && !test_midi) {
        fprintf(stderr, "ERROR: No midi file given\r\n");
        do_syntax();
        return (1);
    }

    if (test_midi) {
        if (midi_file[0] != '\0') {
            fprintf(stderr, "--test_midi and --convert cannot be used together.\n");
            return (1);
        }
    }

    /* check if we only need to convert a file to midi */
    if (midi_file[0] != '\0') {
        const char *real_file = FIND_LAST_DIRSEP(argv[optind]);
        uint32_t size;
        uint8_t *data;

        if (!real_file) real_file = argv[optind];
        else real_file++;

        printf("Converting %s\r\n", real_file);
        if (WildMidi_ConvertToMidi(argv[optind], &data, &size) < 0) {
            fprintf(stderr, "Conversion failed: %s.\r\n", WildMidi_GetError());
            WildMidi_ClearError();
            return (1);
        }

        printf("Writing %s: %u bytes.\r\n", midi_file, size);
        write_midi_output(data, size);
        free(data);
        return (0);
    }

    if (!config_file[0]) {
        strncpy(config_file, WILDMIDI_CFG, sizeof(config_file));
        config_file[sizeof(config_file) - 1] = 0;
    }

    printf("Initializing Sound System (%s)\n", available_outputs[playback_id].name);
    if (wav_file[0] != '\0') {
        // FIXME merge with common case
        if (available_outputs[WAVE_OUT].open_out() == -1) {
            return (1);
        }
    } else {
        if (available_outputs[playback_id].open_out() == -1) {
            return (1);
        }
    }

    libraryver = WildMidi_GetVersion();
    printf("Initializing libWildMidi %ld.%ld.%ld\n\n",
                        (libraryver>>16) & 255,
                        (libraryver>> 8) & 255,
                        (libraryver    ) & 255);
    if (WildMidi_Init(config_file, rate, mixer_options) == -1) {
        fprintf(stderr, "%s\r\n", WildMidi_GetError());
        WildMidi_ClearError();
        return (1);
    }

    printf(" +  Volume up        e  Better resampling    n  Next Midi\n");
    printf(" -  Volume down      l  Log volume           q  Quit\n");
    printf(" ,  1sec Seek Back   r  Reverb               .  1sec Seek Forward\n");
    printf(" m  save as midi     p  Pause On/Off\n\n");

    output_buffer = (int8_t *) malloc(16384);
    if (output_buffer == NULL) {
        fprintf(stderr, "Not enough memory, exiting\n");
        WildMidi_Shutdown();
        return (1);
    }

    wm_inittty();
#ifdef WILDMIDI_AMIGA
    amiga_sysinit();
#endif

    WildMidi_MasterVolume(master_volume);

    while (optind < argc || test_midi) {
        WildMidi_ClearError();
        if (!test_midi) {
            const char *real_file = FIND_LAST_DIRSEP(argv[optind]);

            if (!real_file) real_file = argv[optind];
            else real_file++;
            printf("\rPlaying %s ", real_file);

            midi_ptr = WildMidi_Open(argv[optind]);
            optind++;
            if (midi_ptr == NULL) {
                ret_err = WildMidi_GetError();
                printf(" Skipping: %s\r\n",ret_err);
                continue;
            }
        } else {
            if (test_count == midi_test_max) {
                break;
            }
            test_data = (uint8_t *) malloc(midi_test[test_count].size);
            memcpy(test_data, midi_test[test_count].data,
                    midi_test[test_count].size);
            test_data[25] = test_bank;
            test_data[28] = test_patch;
            midi_ptr = WildMidi_OpenBuffer(test_data, 633);
            test_count++;
            if (midi_ptr == NULL) {
                fprintf(stderr, "\rFailed loading test midi no. %i\r\n", test_count);
                continue;
            }
            printf("\rPlaying test midi no. %i ", test_count);
        }

        wm_info = WildMidi_GetInfo(midi_ptr);

        apr_mins = wm_info->approx_total_samples / (rate * 60);
        apr_secs = (wm_info->approx_total_samples % (rate * 60)) / rate;
        mixer_options = wm_info->mixer_options;
        modes[0] = (mixer_options & WM_MO_LOG_VOLUME)? 'l' : ' ';
        modes[1] = (mixer_options & WM_MO_REVERB)? 'r' : ' ';
        modes[2] = (mixer_options & WM_MO_ENHANCED_RESAMPLING)? 'e' : ' ';
        modes[3] = ' ';
        modes[4] = '\0';

        printf("\r\n[Approx %2um %2us Total]\r\n", apr_mins, apr_secs);
        fprintf(stderr, "\r");

        memset(lyrics,' ',MAX_LYRIC_CHAR);
        memset(display_lyrics,' ',MAX_DISPLAY_LYRICS);

        if (play_from != 0) {
            WildMidi_FastSeek(midi_ptr, &play_from);
            if (play_to < play_from) {
                // Ignore --playto if set less than --playfrom
                play_to = 0;
            }
        }

        while (1) {
            ch = 0;
#ifdef _WIN32
            if (_kbhit()) {
                ch = _getch();
                _putch(ch);
            }
#elif defined(__DJGPP__) || defined(__OS2__) || defined(__EMX__)
            if (kbhit()) {
                ch = getch();
                putch(ch);
            }
#elif defined(WILDMIDI_AMIGA)
            amiga_getch (&ch);
#else
            if (read(STDIN_FILENO, &ch, 1) != 1)
                ch = 0;
#endif
            if (ch) {
                switch (ch) {
                case 'l':
                    WildMidi_SetOption(midi_ptr, WM_MO_LOG_VOLUME,
                                       ((mixer_options & WM_MO_LOG_VOLUME) ^ WM_MO_LOG_VOLUME));
                    mixer_options ^= WM_MO_LOG_VOLUME;
                    modes[0] = (mixer_options & WM_MO_LOG_VOLUME)? 'l' : ' ';
                    break;
                case 'r':
                    WildMidi_SetOption(midi_ptr, WM_MO_REVERB,
                                       ((mixer_options & WM_MO_REVERB) ^ WM_MO_REVERB));
                    mixer_options ^= WM_MO_REVERB;
                    modes[1] = (mixer_options & WM_MO_REVERB)? 'r' : ' ';
                    break;
                case 'e':
                    WildMidi_SetOption(midi_ptr, WM_MO_ENHANCED_RESAMPLING,
                                       ((mixer_options & WM_MO_ENHANCED_RESAMPLING) ^ WM_MO_ENHANCED_RESAMPLING));
                    mixer_options ^= WM_MO_ENHANCED_RESAMPLING;
                    modes[2] = (mixer_options & WM_MO_ENHANCED_RESAMPLING)? 'e' : ' ';
                    break;
                case 'a':
                    WildMidi_SetOption(midi_ptr, WM_MO_TEXTASLYRIC,
                                       ((mixer_options & WM_MO_TEXTASLYRIC) ^ WM_MO_TEXTASLYRIC));
                    mixer_options ^= WM_MO_TEXTASLYRIC;
                    break;
                case 'n':
                    goto NEXTMIDI;
                case 'p':
                    if (inpause) {
                        inpause = 0;
                        fprintf(stderr, "       \r");
                        available_outputs[playback_id].resume_out();
                    } else {
                        inpause = 1;
                        fprintf(stderr, "Paused \r");
                        available_outputs[playback_id].pause_out();
                        continue;
                    }
                    break;
                case 'q':
                    printf("\r\n");
                    if (inpause) goto end2;
                    goto end1;
                case '-':
                    if (master_volume > 0) {
                        master_volume--;
                        WildMidi_MasterVolume(master_volume);
                    }
                    break;
                case '+':
                    if (master_volume < 127) {
                        master_volume++;
                        WildMidi_MasterVolume(master_volume);
                    }
                    break;
                case ',': /* fast seek backwards */
                    if (wm_info->current_sample < rate) {
                        seek_to_sample = 0;
                    } else {
                        seek_to_sample = wm_info->current_sample - rate;
                    }
                    WildMidi_FastSeek(midi_ptr, &seek_to_sample);
                    break;
                case '.': /* fast seek forwards */
                    if ((wm_info->approx_total_samples
                            - wm_info->current_sample) < rate) {
                        seek_to_sample = wm_info->approx_total_samples;
                    } else {
                        seek_to_sample = wm_info->current_sample + rate;
                    }
                    WildMidi_FastSeek(midi_ptr, &seek_to_sample);
                    break;
                case '<':
                    WildMidi_SongSeek (midi_ptr, -1);
                    break;
                case '>':
                    WildMidi_SongSeek (midi_ptr, 1);
                    break;
                case '/':
                    WildMidi_SongSeek (midi_ptr, 0);
                    break;
                case 'm': /* save as midi */ {
                    int8_t *getmidibuffer = NULL;
                    uint32_t getmidisize = 0;
                    int32_t getmidiret = 0;

                    getmidiret = WildMidi_GetMidiOutput(midi_ptr, &getmidibuffer, &getmidisize);
                    if (getmidiret == -1) {
                        fprintf(stderr, "\r\n\nFAILED to convert events to midi\r\n");
                        ret_err = WildMidi_GetError();
                        fprintf(stderr, "%s\r\n",ret_err);
                        WildMidi_ClearError();
                    } else {
                        char *real_file = FIND_LAST_DIRSEP(argv[optind-1]);
                        if (!real_file) real_file = argv[optind];
                        else real_file++;
                        mk_midifile_name(real_file);
                        printf("\rWriting %s: %u bytes.\r\n", midi_file, getmidisize);
                        write_midi_output(getmidibuffer,getmidisize);
                        free(getmidibuffer);
                    }
                  } break;
                case 'k': /* Kareoke */
                          /* Enables/Disables the display of lyrics */
                    kareoke ^= 1;
                    break;
                default:
                    break;
                }
            }

            if (inpause) {
                wm_info = WildMidi_GetInfo(midi_ptr);
                perc_play = (wm_info->current_sample * 100)
                            / wm_info->approx_total_samples;
                pro_mins = wm_info->current_sample / (rate * 60);
                pro_secs = (wm_info->current_sample % (rate * 60)) / rate;
                fprintf(stderr,
                        "%s [%s] [%3i] [%2um %2us Processed] [%2u%%] P  \r",
                        display_lyrics, modes, (int)master_volume, pro_mins,
                        pro_secs, perc_play);
                msleep(5);
                continue;
            }

            if (play_to != 0) {
                if ((wm_info->current_sample + 4096) <= play_to) {
                    samples = 16384;
                } else {
                    samples = (play_to - wm_info->current_sample) << 2;
                    if (!samples) {
                        // We are at or past where we wanted to play to
                        break;
                    }
                }
            }
            else {
                samples = 16384;
            }
            res = WildMidi_GetOutput(midi_ptr, output_buffer, samples);

            if (res <= 0)
                break;

            wm_info = WildMidi_GetInfo(midi_ptr);
            lyric = WildMidi_GetLyric(midi_ptr);

            memmove(lyrics, &lyrics[1], MAX_LYRIC_CHAR - 1);
            lyrics[MAX_LYRIC_CHAR - 1] = ' ';

            if ((lyric != NULL) && (lyric != last_lyric) && (kareoke)) {
                last_lyric = lyric;
                if (last_lyric_length != 0) {
                    memcpy(lyrics, &lyrics[last_lyric_length], MAX_LYRIC_CHAR - last_lyric_length);
                }
                memcpy(&lyrics[MAX_DISPLAY_LYRICS], lyric, strlen(lyric));
                last_lyric_length = strlen(lyric);
            } else {
                if (last_lyric_length != 0) last_lyric_length--;
            }

            memcpy(display_lyrics,lyrics,MAX_DISPLAY_LYRICS);
            display_lyrics[MAX_DISPLAY_LYRICS] = '\0';

            perc_play = (wm_info->current_sample * 100)
                        / wm_info->approx_total_samples;
            pro_mins = wm_info->current_sample / (rate * 60);
            pro_secs = (wm_info->current_sample % (rate * 60)) / rate;
            fprintf(stderr,
                "%s [%s] [%3i] [%2um %2us Processed] [%2u%%] %c  \r",
                display_lyrics, modes, (int)master_volume, pro_mins,
                pro_secs, perc_play, spinner[spinpoint++ % 4]);

            if (available_outputs[playback_id].send_out(output_buffer, res) < 0) {
            /* driver prints an error message already. */
                printf("\r");
                goto end2;
            }
        }
        NEXTMIDI: fprintf(stderr, "\r\n");
        if (WildMidi_Close(midi_ptr) == -1) {
            ret_err = WildMidi_GetError();
            fprintf(stderr, "OOPS: failed closing midi handle!\r\n%s\r\n",ret_err);
        }
        memset(output_buffer, 0, 16384);
        available_outputs[playback_id].send_out(output_buffer, 16384);
    }
end1:
    memset(output_buffer, 0, 16384);
    available_outputs[playback_id].send_out(output_buffer, 16384);
    msleep(5);
end2:
    available_outputs[playback_id].close_out();
    free(output_buffer);
    if (WildMidi_Shutdown() == -1) {
        ret_err = WildMidi_GetError();
        fprintf(stderr, "OOPS: failure shutting down libWildMidi\r\n%s\r\n", ret_err);
        WildMidi_ClearError();
    }
    wm_resetty();

    printf("\r\n");
    return (0);
}

/* helper / replacement functions: */

#if !(defined(_WIN32) || defined(__DJGPP__) || defined(WILDMIDI_AMIGA) || defined(__OS2__) || defined(__EMX__))
static int msleep(unsigned long milisec) {
    struct timespec req = { 0, 0 };
    time_t sec = (int) (milisec / 1000);
    milisec = milisec - (sec * 1000);
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    while (nanosleep(&req, &req) == -1)
        continue;
    return (1);
}
#endif
