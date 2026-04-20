/*
 * AgentBridge - bridge_utils.c
 * String utilities,  type conversions,  logging.
 *
 * Classic Mac OS doesn't have a standard C library in the
 * traditional sense.  These are our safe string operations
 * that work within the constrained environment.
 */

#include <DateTimeUtils.h>
#include <Files.h>
#include <Memory.h>
#include <string.h>

#include "bridge.h"

/* ----------------------------------------------------------------
   Safe string copy with length limit
   ---------------------------------------------------------------- */

void AB_StrCopy(char *dst,  const char *src,  long maxLen)
{
    long i = 0;

    if (maxLen <= 0) return;

    while (i < maxLen - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ----------------------------------------------------------------
   Safe string concatenation with length limit
   ---------------------------------------------------------------- */

void AB_StrCat(char *dst,  const char *src,  long maxLen)
{
    long dstLen = strlen(dst);
    long i = 0;

    while (dstLen + i < maxLen - 1 && src[i] != '\0') {
        dst[dstLen + i] = src[i];
        i++;
    }
    dst[dstLen + i] = '\0';
}

/* ----------------------------------------------------------------
   Case-sensitive string compare
   ---------------------------------------------------------------- */

short AB_StrCmp(const char *a,  const char *b)
{
    while (*a && *b) {
        if (*a != *b) return (*a < *b) ? -1 : 1;
        a++;
        b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/* ----------------------------------------------------------------
   Case-insensitive string compare
   ---------------------------------------------------------------- */

static char ToLower(char c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

short AB_StrCmpNoCase(const char *a,  const char *b)
{
    while (*a && *b) {
        char la = ToLower(*a);
        char lb = ToLower(*b);
        if (la != lb) return (la < lb) ? -1 : 1;
        a++;
        b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/* ----------------------------------------------------------------
   Long integer to decimal string
   ---------------------------------------------------------------- */

void AB_LongToStr(long val,  char *buf)
{
    char    temp[16];
    short   i = 0;
    Boolean neg = false;

    if (val < 0) {
        neg = true;
        val = -val;
    }

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    /* Build digits in reverse */
    while (val > 0 && i < 15) {
        temp[i++] = '0' + (char)(val % 10);
        val /= 10;
    }

    /* Copy in correct order */
    {
        short j = 0;
        if (neg) buf[j++] = '-';
        while (i > 0) {
            buf[j++] = temp[--i];
        }
        buf[j] = '\0';
    }
}

/* ----------------------------------------------------------------
   Decimal string to long integer
   ---------------------------------------------------------------- */

long AB_StrToLong(const char *str)
{
    long    result = 0;
    Boolean neg = false;
    short   i = 0;

    if (str == nil) return 0;

    /* Skip whitespace */
    while (str[i] == ' ' || str[i] == '\t') i++;

    if (str[i] == '-') {
        neg = true;
        i++;
    }
    else if (str[i] == '+') {
        i++;
    }

    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    return neg ? -result : result;
}

/* ----------------------------------------------------------------
   Pascal string to C string conversion
   ---------------------------------------------------------------- */

void AB_PascalToC(const unsigned char *pstr,  char *cstr,  short maxLen)
{
    short len = pstr[0];
    short i;

    if (len > maxLen - 1) len = maxLen - 1;

    for (i = 0; i < len; i++) {
        cstr[i] = (char)pstr[i + 1];
    }
    cstr[len] = '\0';
}

/* ----------------------------------------------------------------
   C string to Pascal string conversion
   ---------------------------------------------------------------- */

void AB_CToPascal(const char *cstr,  unsigned char *pstr)
{
    short len = strlen(cstr);
    short i;

    if (len > 255) len = 255;

    pstr[0] = (unsigned char)len;
    for (i = 0; i < len; i++) {
        pstr[i + 1] = (unsigned char)cstr[i];
    }
}

/* ----------------------------------------------------------------
   Format current date/time as ISO-ish timestamp
   Format: YYYYMMDDTHHmmSS  (no timezone on Classic Mac)
   ---------------------------------------------------------------- */

void AB_FormatTimestamp(char *buf)
{
    unsigned long   secs;
    DateTimeRec     dt;
    char            numBuf[8];

    GetDateTime(&secs);
    SecondsToDate(secs,  &dt);

    /* Year */
    AB_LongToStr(dt.year,  buf);

    /* Month (zero-padded) */
    if (dt.month < 10) AB_StrCat(buf,  "0",  24);
    AB_LongToStr(dt.month,  numBuf);
    AB_StrCat(buf,  numBuf,  24);

    /* Day */
    if (dt.day < 10) AB_StrCat(buf,  "0",  24);
    AB_LongToStr(dt.day,  numBuf);
    AB_StrCat(buf,  numBuf,  24);

    AB_StrCat(buf,  "T",  24);

    /* Hour */
    if (dt.hour < 10) AB_StrCat(buf,  "0",  24);
    AB_LongToStr(dt.hour,  numBuf);
    AB_StrCat(buf,  numBuf,  24);

    /* Minute */
    if (dt.minute < 10) AB_StrCat(buf,  "0",  24);
    AB_LongToStr(dt.minute,  numBuf);
    AB_StrCat(buf,  numBuf,  24);

    /* Second */
    if (dt.second < 10) AB_StrCat(buf,  "0",  24);
    AB_LongToStr(dt.second,  numBuf);
    AB_StrCat(buf,  numBuf,  24);
}

/* ----------------------------------------------------------------
   Simple logging to a file on the shared volume
   ---------------------------------------------------------------- */

void AB_Log(const char *msg)
{
    OSErr   err;
    short   fRefNum;
    Str255  pFilename;
    long    count;
    long    eof;
    char    ts[24];
    char    line[AB_MAX_LINE_LEN];

    if (!gAB.verbose && msg[0] != 'F') {
        /* In non-verbose mode,  only log FATAL messages */
        /* (Quick heuristic: FATAL starts with 'F') */
        return;
    }

    AB_CToPascal("bridge.log",  pFilename);

    /* Try to open existing log file */
    err = HOpenDF(gAB.sharedVRefNum,  gAB.sharedDirID,
                  pFilename,  fsRdWrPerm,  &fRefNum);

    if (err == fnfErr) {
        /* Create it */
        err = HCreate(gAB.sharedVRefNum,  gAB.sharedDirID,
                      pFilename,  'MACS',  'TEXT');
        if (err != noErr) return;

        err = HOpenDF(gAB.sharedVRefNum,  gAB.sharedDirID,
                      pFilename,  fsRdWrPerm,  &fRefNum);
        if (err != noErr) return;
    }
    else if (err != noErr) {
        return;
    }

    /* Seek to end */
    GetEOF(fRefNum,  &eof);
    SetFPos(fRefNum,  fsFromStart,  eof);

    /* Format: [timestamp] message\r */
    AB_FormatTimestamp(ts);
    AB_StrCopy(line,  "[",  sizeof(line));
    AB_StrCat(line,  ts,  sizeof(line));
    AB_StrCat(line,  "] ",  sizeof(line));
    AB_StrCat(line,  msg,  sizeof(line));
    AB_StrCat(line,  "\r",  sizeof(line));

    count = strlen(line);
    FSWrite(fRefNum,  &count,  line);
    FSClose(fRefNum);

    /* Don't flush on every log line -- too slow */
}
