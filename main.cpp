#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dwstation.h"

volatile int running = TRUE;
const char *nowToday = "Build " __DATE__ " " __TIME__;
const char *appName  = "dwstation";

MYSQL *SQLConfigHandler = NULL;

pthread_t tid[THREADS_NUM] = { 0 };

/**
  int main()
*/
int main( int argc, char **argv )
{
  MYSQL_RES *SQLSelectResult = NULL;
  MYSQL_ROW row = NULL; // for current row parsing
  int isSQLInited = FALSE;
  int isSQLConnected = FALSE;
  int stationConfigSelectOK = FALSE;
  int stationConfigOK = FALSE;
  int stationConfigNumFields = FALSE;

  int errThread = 0;

  int connScannerLoopParam = 0;
  int connWeigherLoopParam = 0;
  int scannerLoopParam = 0;
  int weigherLoopParam = 0;
  int checkDBLoopParam = 0;
  int msgQueueLoopParam = 0;

  running = TRUE; // global flag for all threads
  printCurrTime();
  fprintf( stderr, "Start %s, %s\n", appName, nowToday );

  handlersSetup();

  if( openLog() == AWS_FAIL ) {
    fprintf( stderr, "Error: can't open log file, exit\n" );
    exit( AWS_FAIL );
  }

  printLog( "Start %s, %s\n", appName, nowToday );

  // set state
  setState( STATE_BOOTING, STATE_PARAM_OK );
  setState( STATE_STARTING, STATE_PARAM_OK );
  setState( STATE_CONFIG, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_SCANNER, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_WEIGHER, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_DB, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_HTTP, STATE_PARAM_UNKNOWN );

  // setting default http server address
  strcpy( stationConfig.serverAddr, SERVER_ADDR_DEFAULT );

  printLog( "Getting station configuration\n" );

  // Config from MySQL
  if(( SQLConfigHandler = mysql_init( NULL )) == NULL ) {
    printLog( "SQL (config): init error: %s\n", mysql_error( SQLConfigHandler ));
    isSQLInited = FALSE;
    exit( AWS_FAIL );
  } else {
    isSQLInited = TRUE;
    printLog( "SQL (config): init OK\n" );
  }

  if( mysql_real_connect( SQLConfigHandler, MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB, 0, NULL, 0 ) == NULL ) {
    printLog( "SQL (config): connect error: %s\n", mysql_error( SQLConfigHandler ));
    isSQLConnected = FALSE;
    exit( AWS_FAIL );
  } else {
    isSQLConnected = TRUE;
    printLog( "SQL (config): connect OK\n" );
  }

  if( isSQLInited && isSQLConnected ) {
    // Getting config data
    if( mysql_query( SQLConfigHandler
                  , "SELECT http_service_addr,login,password,user_id,scannerIPAddr,scannerPort,weigherIPAddr,weigherPort"
                    " FROM T_dw00conf ORDER BY timestamp DESC limit 1" )) {
      printLog( "SQL (config): select error: %s\n", mysql_sqlstate( SQLConfigHandler ));
      stationConfigSelectOK = FALSE;
    } else {
      stationConfigSelectOK = TRUE;
      printLog( "SQL (config): select config OK\n" );
    }

    if( stationConfigSelectOK ) {
      SQLSelectResult = mysql_store_result( SQLConfigHandler );

      if( SQLSelectResult == NULL ) {
        printLog( "SQL (config): store config result error: %s\n", mysql_sqlstate( SQLConfigHandler ));
      } else {
        stationConfigNumFields = mysql_num_fields( SQLSelectResult );

        if( stationConfigNumFields == ( CFG_LAST_INDEX )) {
          stationConfigOK = TRUE;
          printLog( "SQL (config): station config OK\n" );
          setState( STATE_CONFIG, STATE_PARAM_OK ); // set state
        } else {
          stationConfigOK = FALSE;
          printLog( "SQL (config): station config error: wrong number of fields\n" );
          setState( STATE_CONFIG, STATE_PARAM_NOTOK ); // set state
        }

        if( stationConfigOK ) {
          row = mysql_fetch_row( SQLSelectResult );

          if( row ) {
            if( row[CFG_SERVER_ADDR_INDEX] ) strcpy( stationConfig.serverAddr,    row[CFG_SERVER_ADDR_INDEX] );
            if( row[CFG_LOGIN_INDEX] )       strcpy( stationConfig.login,         row[CFG_LOGIN_INDEX] );
            if( row[CFG_PASSWORD_INDEX] )    strcpy( stationConfig.password,      row[CFG_PASSWORD_INDEX] );
            if( row[CFG_USER_ID_INDEX] )     strcpy( stationConfig.user_id,       row[CFG_USER_ID_INDEX] );
            if( row[CFG_SCANNER_IP_ADDR] )   strcpy( stationConfig.scannerIPAddr, row[CFG_SCANNER_IP_ADDR] );
            if( row[CFG_SCANNER_PORT] )      strcpy( stationConfig.scannerPort,   row[CFG_SCANNER_PORT] );
            if( row[CFG_WEIGHER_IP_ADDR] )   strcpy( stationConfig.weigherIPAddr, row[CFG_WEIGHER_IP_ADDR] );
            if( row[CFG_WEIGHER_PORT] )      strcpy( stationConfig.weigherPort,   row[CFG_WEIGHER_PORT] );
          } else {
            printLog( "SQL fetch row error, exit\n" );
            exit( AWS_FAIL );
          }
        } else {
          printLog( "SQL config structure error, exit\n" );
          exit( AWS_FAIL );
        }

        mysql_free_result( SQLSelectResult );
      }
    } else {
      printLog( "Error: Unable to get configuration from DB, exit\n" );
      exit( AWS_FAIL );
    }
  } else {
    printLog( "Error: SQL init or connect failed, exit\n" );
    exit( AWS_FAIL );
  }

  // station configuration has been successfully processed, you can close the connection to the database
  if( SQLConfigHandler ) {
    printLog( "Connection to MySQL (config) closed\n" );
    mysql_close( SQLConfigHandler );
    SQLConfigHandler = NULL;
  }

  // Threads
  errThread = pthread_create( &(tid[THREAD_CONN_SCANNER]), NULL, &connScannerLoop, (void *) &connScannerLoopParam );
  if( errThread ) printLog( "\nCan't create thread connScannerLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread connScannerLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_CONN_WEIGHER]), NULL, &connWeigherLoop, (void *) &connWeigherLoopParam );
  if( errThread ) printLog( "\nCan't create thread connWeigherLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread connWeigherLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_SCANNER]), NULL, &scannerLoop, (void *) &scannerLoopParam );
  if( errThread ) printLog( "Can't create thread scannerLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread scannerLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_WEIGHER]), NULL, &weigherLoop, (void *) &weigherLoopParam );
  if( errThread ) printLog( "Can't create thread weigherLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread weigherLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_CHECK_DB]), NULL, &checkDBLoop, (void *) &checkDBLoopParam );
  if( errThread ) printLog( "\nCan't create thread checkDBLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread checkDBLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_MSG_QUEUE]), NULL, &msgQueueLoop, (void *) &msgQueueLoopParam );
  if( errThread ) printLog( "\nCan't create thread msgQueueLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread msgQueueLoop created successfully\n" );
  usleep( SLEEP_5MS );

  //  main loop
  while( TRUE ) {
    flushLogFileBuffer();
    checkTimeLogReopen();

    if( connScannerLoopParam == AWS_FAIL ) break;
    if( connWeigherLoopParam == AWS_FAIL ) break;
    if( scannerLoopParam     == AWS_FAIL ) break;
    if( weigherLoopParam     == AWS_FAIL ) break;
    if( checkDBLoopParam     == AWS_FAIL ) break;
    if( msgQueueLoopParam    == AWS_FAIL ) break;

    if( !running ) {
      break;
    }

    setStateCurrTime();
    usleep( SLEEP_10MS );
  }

   // set state
  setState( STATE_STARTING, STATE_PARAM_NOTOK );
  setState( STATE_CONFIG, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_SCANNER, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_WEIGHER, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_DB, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_HTTP, STATE_PARAM_UNKNOWN );

  calcelAllThreads();

  usleep( SLEEP_1S );

  closeAllSockets();
  closeAllSQLConnections();

  fflush( logFile );

  return( AWS_SUCCESS );
}
