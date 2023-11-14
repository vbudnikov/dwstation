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

volatile int logFileUpdated = FALSE;
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

  calcelAllThreads();
  closeAllSockets();
  closeAllSQLConnections();

  curl_global_cleanup();


  setState( STATE_STARTING, STATE_PARAM_NOTOK );
  setState( STATE_CONFIG, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_SCANNER, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_WEIGHER, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_DB, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_HTTP, STATE_PARAM_UNKNOWN );

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
*/
void calcelAllThreads( void )
{
  int s = 0;
  void *res;

  printLog( "Sending threads cancellation request\n" );

  // connScannerLoop
  s = pthread_cancel( tid[THREAD_CONN_SCANNER] );

  if( s == 0 ) {
    printLog( "connScannerLoop: pthread_cancel OK\n" );
  } else {
    printLog( "connScannerLoop: pthread_cancel error = [%d]\n", s );
  }

  s = pthread_join( tid[THREAD_CONN_SCANNER], &res );
  if( res == PTHREAD_CANCELED ) {
    printLog( "connScannerLoop: thread was canceled\n" );
  } else {
    printLog( "connScannerLoop: thread was not cancelled\n", s );
  }

  // connWeigherLoop
  s = pthread_cancel( tid[THREAD_CONN_WEIGHER] );
  if( s == 0 ) {
    printLog( "connWeigherLoop: pthread_cancel OK\n" );
  } else {
    printLog( "connWeigherLoop: pthread_cancel error = [%d]\n", s );
  }

  s = pthread_join( tid[THREAD_CONN_WEIGHER], &res );
  if( res == PTHREAD_CANCELED ) {
    printLog( "connWeigherLoop: thread was canceled\n" );
  } else {
    printLog( "connWeigherLoop: thread was not cancelled\n", s );
  }

  // scannerLoop
  s = pthread_cancel( tid[THREAD_SCANNER] );
  if( s == 0 ) {
    printLog( "scannerLoop: pthread_cancel OK\n" );
  } else {
    printLog( "scannerLoop: pthread_cancel error = [%d]\n", s );
  }

  s = pthread_join( tid[THREAD_SCANNER], &res );
  if( res == PTHREAD_CANCELED ) {
    printLog( "scannerLoop: thread was canceled\n" );
  } else {
    printLog( "scannerLoop: thread was not cancelled\n", s );
  }

  // weigherLoop
  s = pthread_cancel( tid[THREAD_WEIGHER] );
  if( s == 0 ) {
    printLog( "weigherLoop: pthread_cancel OK\n" );
  } else {
    printLog( "weigherLoop: pthread_cancel error = [%d]\n", s );
  }

  s = pthread_join( tid[THREAD_WEIGHER], &res );
  if( res == PTHREAD_CANCELED ) {
    printLog( "weigherLoop: thread was canceled\n" );
  } else {
    printLog( "weigherLoop: thread was not cancelled\n", s );
  }

  // checkDBLoop
  s = pthread_cancel( tid[THREAD_CHECK_DB] );
  if( s == 0 ) {
    printLog( "checkDBLoop: pthread_cancel OK\n" );
  } else {
    printLog( "checkDBLoop: pthread_cancel error = [%d]\n", s );
  }

  s = pthread_join( tid[THREAD_CHECK_DB], &res );
  if( res == PTHREAD_CANCELED ) {
    printLog( "checkDBLoop: thread was canceled\n" );
  } else {
    printLog( "checkDBLoop: thread was not cancelled\n", s );
  }

  // msgQueueLoop
  s = pthread_cancel( tid[THREAD_MSG_QUEUE] );
  if( s == 0 ) {
    printLog( "msgQueueLoop: pthread_cancel OK\n" );
  } else {
    printLog( "msgQueueLoop: pthread_cancel error = [%d]\n", s );
  }

  s = pthread_join( tid[THREAD_MSG_QUEUE], &res );
  if( res == PTHREAD_CANCELED ) {
    printLog( "msgQueueLoop: thread was canceled\n" );
  } else {
    printLog( "msgQueueLoop: thread was not cancelled\n", s );
  }
}

/**
*/
void closeAllSockets( void )
{
  printLog( "Close all sockets\n" );

  // close sockets
  if( scannerSocketFd >= 0 ) {
    printLog( "Connection to scanner closed\n" );
    close( scannerSocketFd );
  }

  if( weigherSocketFd >= 0 ) {
    printLog( "Connection to weigher closed\n" );
    close( scannerSocketFd );
  }
}

/**
*/
void closeAllSQLConnections( void )
{
  printLog( "Close SQL connections\n" );

  if( SQLConfigHandler ) {
    printLog( "Connection to MySQL (station configuration) closed\n" );
    mysql_close( SQLConfigHandler );
  }

  if( SQLNewTUHandler ) {
    printLog( "Connection to MySQL (new TU) closed\n" );
    mysql_close( SQLNewTUHandler );
  }

  if( SQLCheckDBHandler ) {
    printLog( "Connection to MySQL (check DB) closed\n" );
    mysql_close( SQLCheckDBHandler );
  }
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

  logFileUpdated = TRUE; // set flag

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
      logFileUpdated = FALSE; // reset flag
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
/*
void dwExit( int status )
{
  printLog( "%s is going to restart\n", appName );

  calcelAllThreads();
  closeAllSockets();
  closeAllSQLConnections();

  curl_global_cleanup();

  fflush( logFile );

  exit( status );
}
*/

/**
*/
int cmdHandler( char *data )
{
  int retVal = AWS_SUCCESS;
  char cmd[CMD_SZ] = { 0 };
  char cmdParam[CMD_PARAM_SZ] = { 0 };
  char *sepPos = NULL;
  int cmdLen = 0;
  char separator = CMD_SEPARATOR; // '='

  sepPos = strchr( data, separator );

  if( NULL != sepPos ) {
    cmdLen = ( sepPos - data );
    strncpy( cmd, data, cmdLen );
    cmd[cmdLen] = '\0'; // CMD_SEPARATOR;

    strcpy( cmdParam, sepPos + 1 );  // + 1 to exclude separator
    printLog( "%s: Parsing: [%s] => [%s]\n" , __func__, cmd, cmdParam );

    retVal = AWS_SUCCESS;
  } else {
    printLog( "%s: Missing separator\n", __func__ );

    retVal = AWS_FAIL;
  }

  if( 0 == strcmp( cmd, CMD_RESTART_CMD )) {
    if( 0 == strcmp( cmdParam, CMD_RESTART_PARAM )) {
      running = FALSE;
    }
  }

  return retVal;
}

/**
*/
int setState( int state, int param )
{
  int retVal = AWS_SUCCESS;
  FILE *fp = NULL;

  switch( state ) {
    case STATE_BOOTING:
      if(( fp = fopen( FILE_STATE_BOOTING, "w" ))) {
        ( param == STATE_PARAM_OK ) ? fprintf( fp, "OK" ) : fprintf( fp, "в процессе" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    case STATE_STARTING:
      if(( fp = fopen( FILE_STATE_STARTING, "w" ))) {
        ( param == STATE_PARAM_OK ) ? fprintf( fp, "OK" ) : fprintf( fp, "запускается" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    case STATE_CONFIG:
      if(( fp = fopen( FILE_STATE_CONFIG, "w" ))) {
        if ( param == STATE_PARAM_OK ) fprintf( fp, "OK" );
        else if( param == STATE_PARAM_NOTOK ) fprintf( fp, "ошибка" );
        else if( param == STATE_PARAM_UNKNOWN ) fprintf( fp, "???" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    case STATE_CONN_SCANNER:
      if(( fp = fopen( FILE_STATE_CONN_SCANNER, "w" ))) {
        if ( param == STATE_PARAM_OK ) fprintf( fp, "OK" );
        else if( param == STATE_PARAM_NOTOK ) fprintf( fp, "нет связи" );
        else if( param == STATE_PARAM_UNKNOWN ) fprintf( fp, "???" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    case STATE_CONN_WEIGHER:
      if(( fp = fopen( FILE_STATE_CONN_WEIGHER, "w" ))) {
        if ( param == STATE_PARAM_OK ) fprintf( fp, "OK" );
        else if( param == STATE_PARAM_NOTOK ) fprintf( fp, "нет связи" );
        else if( param == STATE_PARAM_UNKNOWN ) fprintf( fp, "???" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    case STATE_CONN_DB:
      if(( fp = fopen( FILE_STATE_CONN_DB, "w" ))) {
        if ( param == STATE_PARAM_OK ) fprintf( fp, "OK" );
        else if( param == STATE_PARAM_NOTOK ) fprintf( fp, "нет связи" );
        else if( param == STATE_PARAM_UNKNOWN ) fprintf( fp, "???" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    case STATE_CONN_HTTP:
      if(( fp = fopen( FILE_STATE_CONN_HTTP, "w" ))) {
        if ( param == STATE_PARAM_OK ) fprintf( fp, "OK" );
        else if( param == STATE_PARAM_NOTOK ) fprintf( fp, "нет связи" );
        else if( param == STATE_PARAM_UNKNOWN ) fprintf( fp, "???" );
        fclose( fp );
      } else {
        retVal = AWS_FAIL;
      }
      break;

    default:
      break;
  }

  printLog( "%s: state [%d], param [%d] set %s\n"
          , __func__, state, param, (retVal == AWS_SUCCESS) ? "OK" : "Fail" );

  return( retVal );
}
