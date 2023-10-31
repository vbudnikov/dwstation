#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "dwstation.h"

const char *nowToday = "Build " __DATE__ " " __TIME__;
const char *appName  = "dwstation";

pthread_t tid[THREADS_NUM] = { 0 };

MYSQL *SQLCfgConn = NULL;
MYSQL_RES *SQLSelectResult = NULL;
int isSQLInited = 0;
int isSQLConnected = 0;
int isSelectCfgOK = 0;
int isCfgOK = 0;

/**
  int main()
*/
int main( int argc, char **argv )
{
  int errThread = 0;

  printCurrTime();
  fprintf( stderr, "Start %s, %s\n", appName, nowToday );

  handlersSetup();
  openLog();

  printLog( "Start %s, %s\n", appName, nowToday );

  strcpy( stationConfig.serverAddr, SERVER_ADDR_DEFAULT ); // setting default http server address

  printLog( "Getting station configuration\n" );

  // Config from MySQL
  if(( SQLCfgConn = mysql_init( NULL )) == NULL ) {
    printLog( "SQL init error: %s\n", mysql_error( SQLCfgConn ));
    isSQLInited = FALSE;
  } else {
    isSQLInited = TRUE;
    printLog( "SQL init OK\n" );
  }

  if( mysql_real_connect( SQLCfgConn, "127.0.0.1", "dwstation", "dwstation", "dwstation", 0, NULL, 0 ) == NULL ) {
    printLog( "SQL connect error: %s\n", mysql_error( SQLCfgConn ));
    isSQLConnected = FALSE;
  } else {
    isSQLConnected = TRUE;
    printLog( "SQL connect OK\n" );
  }

  if( isSQLInited && isSQLConnected ) {
    // Getting station configuration data
    if( mysql_query( SQLCfgConn
                  , "SELECT http_service_addr,login,password,user_id,scannerIPAddr,scannerPort,weigherIPAddr,weigherPort" \
                    " FROM T_dw00conf ORDER BY timestamp DESC limit 1" )) {
      printLog( "Station config: SELECT query error: %s\n", mysql_sqlstate( SQLCfgConn ));
      isSelectCfgOK = FALSE;
    } else {
      isSelectCfgOK = TRUE;
      printLog( "SQL select config OK\n" );
    }

    if( isSelectCfgOK ) {
      SQLSelectResult = mysql_store_result( SQLCfgConn );
      if( SQLSelectResult == NULL ) {
        printLog( "SLQ store config result error: %s\n", mysql_sqlstate( SQLCfgConn ));
      } else {
        int num_fields = mysql_num_fields( SQLSelectResult );
        if( num_fields == ( CFG_LAST_INDEX )) {
          isCfgOK = TRUE;
          printLog( "SQL store config result OK\n" );
        } else {
          isCfgOK = FALSE;
          printLog( "SQL store config result error\n" );
        }

        MYSQL_ROW row = NULL;
        row = mysql_fetch_row( SQLSelectResult );
        if( row ) {
          if( row[CFG_SERVER_ADDR_INDEX] ) strcpy( stationConfig.serverAddr,    row[CFG_SERVER_ADDR_INDEX] );
          if( row[CFG_LOGIN_INDEX] )       strcpy( stationConfig.login,         row[CFG_LOGIN_INDEX] );
          if( row[CFG_PASSWORD_INDEX] )    strcpy( stationConfig.password,      row[CFG_PASSWORD_INDEX] );
          if( row[CFG_USER_ID_INDEX] )     strcpy( stationConfig.user_id,       row[CFG_USER_ID_INDEX] );
          if( row[CFG_SCANNER_IP_ADDR] )   strcpy( stationConfig.scannerIPAddr, row[CFG_SCANNER_IP_ADDR] );
          if( row[CFG_SCANNER_PORT] )      strcpy( stationConfig.scannerPort,   row[CFG_SCANNER_PORT] );
          if( row[CFG_WEIGHER_IP_ADDR] )   strcpy( stationConfig.weigherIPAddr, row[CFG_WEIGHER_IP_ADDR] );
          if( row[CFG_WEIGHR_PORT] )       strcpy( stationConfig.weigherPort,   row[CFG_WEIGHR_PORT] );
        }
      }
    } else {
      printLog( "Error: Unable to get configuration from database, exit\n" );
      exit( 1 );
    }
  }


  // Threads
  errThread = pthread_create( &(tid[THREAD_CONN_SCANNER]), NULL, &connScannerLoop, NULL );
  if( errThread ) printLog( "\nCan't create thread connScannerLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread connScannerLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_CONN_WEIGHER]), NULL, &connWeigherLoop, NULL );
  if( errThread ) printLog( "\nCan't create thread connWeigherLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread connWeigherLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_SCANNER]), NULL, &scannerLoop, NULL );
  if( errThread ) printLog( "Can't create thread scannerLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread scannerLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_WEIGHER]), NULL, &weigherLoop, NULL );
  if( errThread ) printLog( "Can't create thread weigherLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread weigherLoop created successfully\n" );
  usleep( SLEEP_5MS );

  errThread = pthread_create( &(tid[THREAD_CHECK_DB]), NULL, &checkDBLoop, NULL );
  if( errThread ) printLog( "\nCan't create thread checkDBLoop: [%s]\n", strerror( errThread ));
  else printLog( "Thread checkDBLoop created successfully\n" );
  usleep( SLEEP_5MS );

  while( TRUE ) {
    flushLogFileBuffer();
    check12();

    usleep( SLEEP_10MS );
  }

  if( SQLCfgConn ) mysql_close( SQLCfgConn );

  return 0;
}
