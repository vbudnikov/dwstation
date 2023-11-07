#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
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
  printLog( "\nCtrl-C detected\n" );

  if( scannerSocketFd >= 0 ) {
    printLog( "Connection to scanner closed\n" );
    close( scannerSocketFd );
  }

  if( weigherSocketFd >= 0 ) {
    printLog( "Connection to weigher closed\n" );
    close( scannerSocketFd );
  }

  if( SQLConnConfig ) {
    printLog( "Connection to MySQL (station configuration) closed\n" );
    mysql_close( SQLConnConfig );
  }

  if( SQLConnNewTU ) {
    printLog( "Connection to MySQL (new TU) closed\n" );
    mysql_close( SQLConnNewTU );
  }

  if( SQLConnCheckSend ) {
    printLog( "Connection to MySQL (check send) closed\n" );
    mysql_close( SQLConnCheckSend );
  }

  curl_global_cleanup();

  printLog( "%s terminated\n", appName );
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
  uint16_t i = 0;
  const uint16_t nAttempts = 10;

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
  static uint8_t semaphore = 0;
  time_t T = time( NULL );
  struct tm tm = *localtime( &T );
  char timestr[TIME_STR_SZ] = { 0 };
  char logBuf[LOG_BUF_SZ] = { 0 };
  static FILE *errLogFile = NULL;
  char *logBufLoweredPtr = NULL;
  const uint8_t nCnt = 8;

  logFileUpdated = 1; // set flag

  gettimeofday( &tsRawtime, NULL );

  sprintf( timestr, "%04d.%02d.%02d %02d:%02d:%02d.%06ld\t"
         , tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tsRawtime.tv_usec );

  va_list ptr;
  va_start( ptr, format );

  if( semaphore ) {
    for ( uint8_t i = 1; i < nCnt; i++ ) {
      usleep( i * 1000 );

      if( semaphore == FALSE ) {
        semaphore = TRUE;
        break;
      }
    }
  } else {
    semaphore = TRUE;
  }

  fprintf( logFile, "%s", timestr );
  vsprintf( logBuf, format, ptr );

  logBufLoweredPtr = logBuf;

  // lower all characters in string buffer
  while( *logBufLoweredPtr++ ) *logBufLoweredPtr = tolower( *logBufLoweredPtr );

  vfprintf( logFile, format, ptr );
  if( DEBUG ) vfprintf( stderr, format, ptr ); // ### DEBUG


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
int checkTimeLogReopen( void )
{
  static uint8_t hourPrev = 0;
  static uint8_t minutePrev = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;

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
void setNonblock( int socket )
{
  int flags = -1;

  flags = fcntl( socket, F_GETFL, 0 );

  if( flags == -1 ) {
    printLog( "Error: (O_NONBLOCK) fcntl returns -1, exit\n" );
    exit( 1 );
  }

  fcntl( socket, F_SETFL, flags | O_NONBLOCK );
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
      // printLog( "Flushing log file buffer\n" ); // ###
      fflush( logFile );
    }
  }
}

/**
*/
int flushRecvBuffer( int socket )
{
  uint16_t recvCnt = 0;
  char buf[SCANNER_RECV_BUF_SZ] = { 0 };

  fprintf( stderr, "Flushing receive buffer\n" );

  while( recv( socket, buf, sizeof( buf ), 0 ) < 0 ) {
    recvCnt++;
    if( recvCnt >= SCANNER_MAX_RECV_CNT ) break;
    usleep( SLEEP_10MS );
  }

  return( 0 );
}

/**
  -----------------------------------------------------------------------------
  Function name:  getTimeDeltaMS
  -----------------------------------------------------------------------------
  Purpose:        Calculate time difference in milliseconds
  Parameters:     t0: event start time
                  t1: event end time
  Return:         Time difference between events t0 and t1
*/
unsigned int getTimeDeltaMS( unsigned int t0, unsigned int t1 )
{
  unsigned int retVal = 0;

  if( t1 >= t0 ) {
    retVal = t1 - t0;
  } else {
    retVal = UINT_MAX - t0 + t1;
  }

  return retVal;
}

/**
*/
void dwExit( int status )
{
  if( SQLConnConfig ) {
    printLog( "Connection to MySQL (station configuration) closed\n" );
    mysql_close( SQLConnConfig );
  }

  if( SQLConnNewTU ) {
    printLog( "Connection to MySQL (new TU) closed\n" );
    mysql_close( SQLConnNewTU );
  }

  if( SQLConnCheckSend ) {
    printLog( "Connection to MySQL (check send) closed\n" );
    mysql_close( SQLConnCheckSend );
  }

  exit( status );
}

