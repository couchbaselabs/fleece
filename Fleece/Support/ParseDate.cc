//
//  ParseDate.c
//  Fleece
//
//  Copyright (c) 2017 Couchbase, Inc All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//

/*
 ** 2003 October 31
 **
 ** The author disclaims copyright to this source code.  In place of
 ** a legal notice, here is a blessing:
 **
 **    May you do good and not evil.
 **    May you find forgiveness for yourself and forgive others.
 **    May you share freely, never taking more than you give.
 **
 *************************************************************************
 ** This file contains the C functions that implement date and time
 ** functions for SQLite.
 **
 ** There is only one exported symbol in this file - the function
 ** sqlite3RegisterDateTimeFunctions() found at the bottom of the file.
 ** All other code has file scope.
 **
 ** SQLite processes all times and dates as Julian Day numbers.  The
 ** dates and times are stored as the number of days since noon
 ** in Greenwich on November 24, 4714 B.C. according to the Gregorian
 ** calendar system.
 **
 ** 1970-01-01 00:00:00 is JD 2440587.5
 ** 2000-01-01 00:00:00 is JD 2451544.5
 **
 ** This implemention requires years to be expressed as a 4-digit number
 ** which means that only dates between 0000-01-01 and 9999-12-31 can
 ** be represented, even though julian day numbers allow a much wider
 ** range of dates.
 **
 ** The Gregorian calendar system is used for all dates and times,
 ** even those that predate the Gregorian calendar.  Historians usually
 ** use the Julian calendar for dates prior to 1582-10-15 and for some
 ** dates afterwards, depending on locale.  Beware of this difference.
 **
 ** The conversion algorithms are implemented based on descriptions
 ** in the following text:
 **
 **      Jean Meeus
 **      Astronomical Algorithms, 2nd Edition, 1998
 **      ISBM 0-943396-61-1
 **      Willmann-Bell, Inc
 **      Richmond, Virginia (USA)
 */

#include "ParseDate.hh"
#include "date/date.h"
#include "FleeceException.hh"

#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

#ifdef WIN32
#include <Windows.h>
#endif

using namespace std;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace date;

typedef uint8_t u8;
typedef int64_t sqlite3_int64;

#define sqlite3Isdigit isdigit
#define sqlite3Isspace isspace

#define LONG_MONTHS 0x15AA          // 1 bits for months with 31 days


/*
 ** A structure for holding a single date and time.
 */
typedef struct DateTime DateTime;
struct DateTime {
    sqlite3_int64 iJD; /* The julian day number times 86400000 */
    int Y, M, D;       /* Year, month, and day */
    int h, m;          /* Hour and minutes */
    int tz;            /* Timezone offset in minutes */
    double s;          /* Seconds */
    char validYMD;     /* True (1) if Y,M,D are valid */
    char validHMS;     /* True (1) if h,m,s are valid */
    char validJD;      /* True (1) if iJD is valid */
    char validTZ;      /* True (1) if tz is valid */
};


/*
 ** Convert zDate into one or more integers.  Additional arguments
 ** come in groups of 5 as follows:
 **
 **       N       number of digits in the integer
 **       min     minimum allowed value of the integer
 **       max     maximum allowed value of the integer
 **       nextC   first character after the integer
 **       pVal    where to write the integers value.
 **
 ** Conversions continue until one with nextC==0 is encountered.
 ** The function returns the number of successful conversions.
 */
static int getDigits(const char *zDate, ...){
    va_list ap;
    int val;
    int N;
    int min;
    int max;
    int nextC;
    int *pVal;
    int cnt = 0;
    va_start(ap, zDate);
    do{
        N = va_arg(ap, int);
        min = va_arg(ap, int);
        max = va_arg(ap, int);
        nextC = va_arg(ap, int);
        pVal = va_arg(ap, int*);
        val = 0;
        while( N-- ){
            if( !sqlite3Isdigit(*zDate) ){
                goto end_getDigits;
            }
            val = val*10 + *zDate - '0';
            zDate++;
        }
        if( val<min || val>max || (nextC!=0 && nextC!=*zDate) ){
            goto end_getDigits;
        }
        *pVal = val;
        zDate++;
        cnt++;
    }while( nextC );
end_getDigits:
    va_end(ap);
    return cnt;
}

/*
 ** Parse a timezone extension on the end of a date-time.
 ** The extension is of the form:
 **
 **        (+/-)HH:MM or (+/-)HHMM
 **
 ** Or the "zulu" notation:
 **
 **        Z
 **
 ** If the parse is successful, write the number of minutes
 ** of change in p->tz and return 0.  If a parser error occurs,
 ** return non-zero.
 **
 ** A missing specifier is not considered an error.
 */
static int parseTimezone(const char *zDate, DateTime *p){
    int sgn = 0;
    int nHr, nMn;
    int c;
    while( sqlite3Isspace(*zDate) ){ zDate++; }
    p->tz = 0;
    p->validTZ = 0;
    c = *zDate;
    if( c=='-' ){
        sgn = -1;
    }else if( c=='+' ){
        sgn = +1;
    }else if( c=='Z' || c=='z' ){
        zDate++;
        goto zulu_time;
    }else{
        return c!=0;
    }
    zDate++;
    
    if( getDigits(zDate, 2, 0, 14, 0, &nHr) != 1 ) {
        return 1;
    }
    
    zDate += 2;
    
    if (*zDate == ':') {
        zDate++;
    }
    
    if( getDigits(zDate, 2, 0, 59, 0, &nMn) != 1 ) {
        return 1;
    }
    
    zDate += 2;
    p->tz = sgn*(nMn + nHr*60);
zulu_time:
    while( sqlite3Isspace(*zDate) ){ zDate++; }
    p->validTZ = *zDate==0;
    return *zDate!=0;
}

/*
 ** Parse times of the form HH:MM or HH:MM:SS or HH:MM:SS.FFFF.
 ** The HH, MM, and SS must each be exactly 2 digits.  The
 ** fractional seconds FFFF can be one or more digits.
 **
 ** Return 1 if there is a parsing error and 0 on success.
 */
static int parseHhMmSs(const char *zDate, DateTime *p){
    int h, m, s;
    double ms = 0.0;
    if( getDigits(zDate, 2, 0, 24, ':', &h, 2, 0, 59, 0, &m)!=2 ){
        return 1;
    }
    zDate += 5;
    if( *zDate==':' ){
        zDate++;
        if( getDigits(zDate, 2, 0, 59, 0, &s)!=1 ){
            return 1;
        }
        zDate += 2;
        if( *zDate=='.' && sqlite3Isdigit(zDate[1]) ){
            double rScale = 1.0;
            zDate++;
            while( sqlite3Isdigit(*zDate) ){
                ms = ms*10.0 + *zDate - '0';
                rScale *= 10.0;
                zDate++;
            }
            ms /= rScale;
        }
    }else{
        s = 0;
    }
    p->validJD = 0;
    p->validHMS = 1;
    p->h = h;
    p->m = m;
    p->s = s + ms;
    if( parseTimezone(zDate, p) ) return 1;
    return 0;
}

/*
 ** Convert from YYYY-MM-DD HH:MM:SS to julian day.  We always assume
 ** that the YYYY-MM-DD is according to the Gregorian calendar.
 **
 ** Reference:  Meeus page 61
 */
static void computeJD(DateTime *p){
    int Y, M, D, A, B, X1, X2;

    if( p->validJD ) return;
    if( p->validYMD ){
        Y = p->Y;
        M = p->M;
        D = p->D;
    }else{
        Y = 2000;  /* If no YMD specified, assume 2000-Jan-01 */
        M = 1;
        D = 1;
    }
    if( M<=2 ){
        Y--;
        M += 12;
    }
    A = Y/100;
    B = 2 - A + (A/4);
    X1 = 36525*(Y+4716)/100;
    X2 = 306001*(M+1)/10000;
    p->iJD = (sqlite3_int64)((X1 + X2 + D + B - 1524.5 ) * 86400000);
    p->validJD = 1;
    if( p->validHMS ){
        p->iJD += p->h*3600000 + p->m*60000 + (sqlite3_int64)round(p->s*1000);
        if( p->validTZ ){
            p->iJD -= p->tz*60000;
            p->validYMD = 0;
            p->validHMS = 0;
            p->validTZ = 0;
        }
    }
}

// Convert DateTime to tm WITHOUT calling mktime (possibly invalid until then)
static inline struct tm FromDate(DateTime* p) {
    struct tm local_time {};
    local_time.tm_sec = (int)p->s;
    local_time.tm_min = p->m;
    local_time.tm_hour = p->h;
    local_time.tm_mday = p->D;
    local_time.tm_mon = p->M - 1;
    local_time.tm_year = p->Y - 1900;
    local_time.tm_isdst = -1;

    return local_time;
}

static void inject_local_tz(DateTime* p)
{
    struct tm local_time = FromDate(p);
    auto offset = floor<minutes>(fleece::GetLocalTZOffset(&local_time, false));
    p->validTZ = 1;
    p->tz = (int)offset.count();
}

/*
 ** Parse dates of the form
 **
 **     YYYY-MM-DD HH:MM:SS.FFF
 **     YYYY-MM-DD HH:MM:SS
 **     YYYY-MM-DD HH:MM
 **     YYYY-MM-DD
 **
 ** Write the result into the DateTime structure and return 0
 ** on success and 1 if the input string is not a well-formed
 ** date.
 */
static int parseYyyyMmDd(const char *zDate, DateTime *p){
    int Y, M, D, neg;

    if( zDate[0]=='-' ){
        zDate++;
        neg = 1;
    }else{
        neg = 0;
    }
    if( getDigits(zDate,4,0,9999,'-',&Y,2,1,12,'-',&M,2,1,31,0,&D)!=3 ){
        return 1;
    }
    if (_usuallyFalse(D >= 29)) {
        // Check for days past the end of the month:
        if (_usuallyFalse(M == 2)) {
            if (_usuallyFalse(D > 29)) {
                return 1;
            } else if (_usuallyFalse(Y % 4 != 0 || (Y % 100 == 0 && Y % 400 != 0))) {
                return 1;
            }
        } else if (_usuallyFalse(D > 30 && !(LONG_MONTHS & (1 << M)))) {
            return 1;
        }
    }
    zDate += 10;
    while( sqlite3Isspace(*zDate) || 'T'==*(u8*)zDate ){ zDate++; }
    if( parseHhMmSs(zDate, p)==0 ){
        /* We got the time */
    }else if( *zDate==0 ){
        p->validHMS = 1;
        p->h = p->m = 0;
        p->s = 0.0;
        p->validTZ = 0;
    }else{
        return 1;
    }
    p->validJD = 0;
    p->validYMD = 1;
    p->Y = neg ? -Y : Y;
    p->M = M;
    p->D = D;
    if( p->validTZ ){
        computeJD(p);
    } else {
        inject_local_tz(p);
    }
    return 0;
}


#pragma mark END OF SQLITE CODE
#pragma mark -


namespace fleece {

    int64_t ParseISO8601Date(const char* zDate) {
        DateTime x;
        if (parseYyyyMmDd(zDate,&x))
            return kInvalidDate;
        computeJD(&x);
        return x.iJD - 210866760000000;
    }

    int64_t ParseISO8601Date(fleece::slice date) {
        return ParseISO8601Date(string(date).c_str());
    }

    slice FormatISO8601Date(char buf[], int64_t time, bool asUTC) {
        if (time == kInvalidDate) {
            *buf = 0;
            return nullslice;
        }

        stringstream timestream;
        auto tp = local_time<milliseconds>(milliseconds(time));
        auto totalSec = floor<seconds>(tp);
        auto totalMs = floor<milliseconds>(tp);
        struct tm tmpTime = FromTimestamp(totalSec.time_since_epoch());
        auto offset = GetLocalTZOffset(&tmpTime, true);
        if(asUTC || offset == 0min) {
            if(totalSec == totalMs) {
                timestream << format("%FT%TZ", floor<seconds>(tp));
            } else {
                timestream << format("%FT%TZ", floor<milliseconds>(tp));
            }
        } else {
            tp += offset;
            auto h = duration_cast<hours>(offset);
            auto m = offset - h;
            timestream.fill('0');
            if(totalSec == totalMs) {
                timestream << format("%FT%T", floor<seconds>(tp));
            } else {
                timestream << format("%FT%T", floor<milliseconds>(tp));
            }
               
            timestream << setw(3) << std::internal << showpos << h.count()
                << noshowpos << setw(2) << (int)abs(m.count());
        }
        
        memcpy(buf, timestream.str().c_str(), timestream.str().size());
        return {buf, timestream.str().length()};
    }

    struct tm FromTimestamp(seconds timestamp) {
        local_seconds tp { timestamp };
        auto dp = floor<days>(tp);
        year_month_day ymd { dp };
        auto hhmmss = make_time(tp - dp);

        struct tm local_time {};
        local_time.tm_sec = (int)hhmmss.seconds().count();
        local_time.tm_min = (int)hhmmss.minutes().count();
        local_time.tm_hour = (int)hhmmss.hours().count();
        local_time.tm_mday = (int)static_cast<unsigned>(ymd.day());
        local_time.tm_mon = (int)static_cast<unsigned>(ymd.month()) - 1;
        local_time.tm_year = static_cast<int>(ymd.year()) - 1900;
        local_time.tm_isdst = -1;

        return local_time;
    }

    seconds GetLocalTZOffset(struct tm* localtime, bool input_utc) {
        // This method is annoyingly delicate, and warrants lots of explanation

        // First, call tzset so that the needed information is populated
        // by the C runtime
#if !defined(_MSC_VER) || WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
        // Let's hope this works on UWP since Microsoft has removed the
        // tzset and _tzset functions from UWP
        static std::once_flag once;
        std::call_once(once, []
        {
#ifdef _MSC_VER
            _tzset();
#else
            tzset();
#endif
        });
#endif

        // Find the system time zone's offset from UTC
        // Windows -> _get_timezone
        // Others -> global timezone variable
        //      https://linux.die.net/man/3/tzset (System V-like / XSI)
        //      http://www.unix.org/version3/apis/t_9.html (Unix v3)
        //
        // NOTE: These values are the opposite of what you would expect, being defined
        // as seconds WEST of GMT (so UTC-8 would be 28,800, not -28,800)
#ifdef WIN32
        long s;
        throwIf(_get_timezone(&s) != 0, fleece::InternalError, "Unable to query local system time zone");
        auto offset = seconds(-s);
#elif defined(__DARWIN_UNIX03) || defined(__ANDROID__) || defined(_XOPEN_SOURCE) || defined(_SVID_SOURCE)
        auto offset = seconds(-timezone);
#else
        #error Unimplemented GetLocalTZOffset
#endif

        // Apply the timezone offset first to get the proper time
        // in the current timezone (no-op if local time was passed)
        if(input_utc) {
            localtime->tm_sec -= offset.count();
        }

        // In order to consider DST, mktime needs to be called.
        // However this has the caveat that it will never be
        // clear if the "before" or "after" DST is desired in the case
        // of a rollback of clocks in which an hour is repeated.  Moral
        // of the story:  USE TIME ZONES IN YOUR DATE STRINGS!
        if(mktime(localtime) != -1) {
            offset += hours(localtime->tm_isdst);
        }

        return offset;
    }

}
