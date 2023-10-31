#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <curl/curl.h>

#include "dwstation.h"

volatile int logFileUpdated = 0;
struct timeval tsRawtime;
FILE *logFile = NULL;
char errLogFileName[LOG_FILE_NAME_SZ] = { 0 };

static char logsPath[]    = "/mnt/ssd/dwstation/logs/";
static char errLogsPath[] = "/mnt/ssd/dwstation/errlogs/";

/*
*/
static void ctrlCHandler( int signum )
{
  fprintf( stderr, "\nCtrl-C detected\n%s terminated\n", appName );

  if( scannerSocketFd >= 0 ) {
    printLog( "Connection to scanner closed\n" );
    close( scannerSocketFd );
  }

  if( weigherSocketFd >= 0 ) {
    printLog( "Connection to weigher closed\n" );
    close( scannerSocketFd );
  }

  if( SQLCfgConn ) {
    printLog( "Connection to MySQL closed\n" );
    mysql_close( SQLCfgConn );
  }

  printLog( "\nCtrl-C detected\n%s terminated\n", appName );

  curl_global_cleanup();
  fflush( logFile );

  (void)( signum );
  exit( 0 );
}

/**
  static void setup_handlers( void )
*/
void handlersSetup( void )
{
  struct sigaction act;
  act.sa_handler = ctrlCHandler;
  sigaction( SIGINT,  &act, NULL );
  sigaction( SIGTERM, &act, NULL );
  sigaction( SIGKILL, &act, NULL );
}

/**
  void PrintCurrTime( void )
*/
void printCurrTime( void )
{
  time_t T = time( NULL );
  struct tm tm = *localtime( &T );
  char timestr[TIME_STR_SZ];
  struct timeval tsRawtime;

  gettimeofday( &tsRawtime, NULL );
  sprintf( timestr, "%02d.%02d.%04d %02d:%02d:%02d.%06ld  "
         , tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,tm.tm_hour, tm.tm_min, tm.tm_sec, tsRawtime.tv_usec );

  fprintf( stderr, "%s", timestr );
}

/**
*/
int openLog( void )
{
  time_t T = time( NULL );
  struct tm tm = *localtime( &T );
  char logFileName[LOG_FILE_NAME_SZ] = { 0 };
  char sysCmd[SYS_CMD_SZ] = { 0 };
  unsigned int i = 0;
  unsigned int nAttempts = 10;

  sprintf( errLogFileName, "%sdwstation-error-%04d-%02d-%02d-%02d.%02d.%02d.log"
         , errLogsPath, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec );
  sprintf( logFileName,    "%sdwstation-%04d-%02d-%02d-%02d.%02d.%02d.log"
         , logsPath,    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec );


  for( i = 0; i < nAttempts; i++ ) {
    logFile = fopen( logFileName, "w+" );

    if( logFile ) {
    	return( 0 ); // ok
    } else {
      fprintf( stderr, "\n[-] Failed to open file %s\n", logFileName );
      fprintf( stderr, "Error description is: %s\n", strerror( errno ));

      sprintf( sysCmd, "rm -f %s*.log", logsPath );
      system( sysCmd );
      usleep( i * SLEEP_100MS );
    }
  }

  return( 1 ); // error
}

/**
*/
void printLog( const char *format, ... )
{
  static unsigned short semaphore = 0;
  time_t T = time( NULL );
  struct tm tm = *localtime( &T );
  char timestr[TIME_STR_SZ] = { 0 };
  char logBuf[LOG_BUF_SZ] = { 0 };
  static FILE *errLogFile = NULL;
  char *logBufLoweredPtr = NULL;

  logFileUpdated = 1; // set flag

  gettimeofday( &tsRawtime, NULL );

  sprintf( timestr, "%04d.%02d.%02d %02d:%02d:%02d.%06ld\t"
         , tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tsRawtime.tv_usec );

  va_list ptr;
  va_start( ptr, format );

  if( semaphore ) {
    for ( int i = 1; i < 8; i++ ) {
      usleep( i * 1000 );

      if( semaphore == 0 ) {
        semaphore = 1;
        break;
      }
    }
  } else {
    semaphore = 1;
  }

  fprintf( logFile, "%s", timestr );
  vsprintf( logBuf, format, ptr );

  logBufLoweredPtr = logBuf;

  // lower all characters in string buffer
  while( *logBufLoweredPtr++ ) *logBufLoweredPtr = tolower( *logBufLoweredPtr );

  vfprintf( logFile, format, ptr );

  if( strstr( logBuf, "error" )) {
    errLogFile = fopen( errLogFileName, "a" );

    if( !errLogFile ) {
      fprintf( stderr, "\n[-] %s: Failed to open file %s\n", __func__, errLogFileName );
      fprintf( logFile, "\n[-] %s: Failed to open file %s\n", __func__, errLogFileName );
      fprintf( stderr, "Error description is : %s\n", strerror( errno ));
      fprintf( logFile, "Error description is : %s\n", strerror( errno ));
      exit( 1 );

    } else {
      fprintf( errLogFile, "%s", timestr );
      vfprintf( errLogFile, format, ptr );
      fclose( errLogFile );
    }
  }

  semaphore = 0;
  va_end( ptr );
}

/**
*/
int check12( void )
{
  static int hourPrev = 0;
  static int minutePrev = 0;
  int hour = 0;
  int minute = 0;

  time_t T = time( NULL );
  struct tm tm = *localtime( &T );

  hour =  tm.tm_hour;
  minute = tm.tm_min;

  if((( hour == 12 ) && ( minute == 0 )) && (( hourPrev == 11 ) && ( minutePrev == 59 ))) {
    hourPrev = hour;
    minutePrev = minute;

    printLog( "REOPEN log-files! ERROR %s, version: %s\n", appName, nowToday );

    fflush( logFile );
    fsync( fileno( logFile ));
    fclose( logFile );
    openLog();

    printLog( "REOPEN log-files! ERROR %s, version: %s\n", appName, nowToday );

    return( 1 );
  } else {
    hourPrev = hour;
    minutePrev = minute;
  }

  return( 0 );
}

/**
*/
void flushLogFileBuffer( void )
{
  static time_t timePrev = 0;
  static time_t timeCurr = 0;

  timeCurr = time( NULL );
  if(( timeCurr - timePrev ) >= LOG_FILE_FLUSH_INTERVAL ) {
    timePrev = timeCurr;

    if( logFileUpdated ) {
      logFileUpdated = 0; // reset flag
      printLog( "Flushing log file buffer\n" );
      fflush( logFile );
    }
  }
}
