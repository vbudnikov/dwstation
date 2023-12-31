#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <curl/curl.h>
#include <netinet/tcp.h>

#include "dwstation.h"

#define REQUEST_SELECT_NEW_TU   "SELECT uid,barcode,weight,timestamp,status FROM T_dw00resp WHERE status=0 ORDER BY timestamp ASC limit 1"

MYSQL *SQLNewTUHandler = NULL;
MYSQL *SQLCheckDBHandler = NULL;

int scannerSocketFd = -1;
int weigherSocketFd = -1;

static time_t sensorEventRAWTime = 0;
static time_t sensorEventRAWTimePrev = 0;

static time_t sensorEventRAWTimeMS = 0;
static time_t sensorEventRAWTimeMSPrev = 0;

volatile static int sensorScannerEventFlag = FALSE;
volatile static int sensorWeigherEventFlag = FALSE;

volatile static int scannerConnected = FALSE;
volatile static int weigherConnected = FALSE;

struct stStationConfig stationConfig;
struct stWeightRecord  weightRecord;
struct stCurrTUParam   currTUParam;

int msqid = 0;
typedef struct mqbuf {
  long mtype;
  char mtext[MSG_SZ];
} messageBuf;

/**
*/
void pinSetup( void )
{
  wiringPiSetup();
  pinMode( PIN_DAT, INPUT );

  // wiringPiISR( pin_dat, INT_EDGE_RISING, &sensorEvent );  // INT_EDGE_BOTH
  wiringPiISR( PIN_DAT, INT_EDGE_FALLING, &sensorEvent );  // changed to falling edge for refletive sensor

  printLog( "Using GPIO pin: PIN%d\n", PIN_DAT );

  // volatile: setting time values
  sensorEventRAWTime = time( NULL );
  sensorEventRAWTimePrev = sensorEventRAWTime;

  sensorEventRAWTimeMS = millis();
  sensorEventRAWTimeMSPrev = sensorEventRAWTimeMS;

  // volatile: need to initialize
  sensorScannerEventFlag = FALSE;
  sensorWeigherEventFlag = FALSE;
}

/**
*/
void sensorEvent( void )
{
  int pinData = 0;
  time_t deltaMS = 0;

  usleep( SENSOR_EVENT_DELAY );

  sensorEventRAWTimeMS = millis();

  pinData = digitalRead( PIN_DAT );

  if( pinData == 1 ) {
    return; // bad interrupt
  }

  deltaMS = getTimeDeltaMS( sensorEventRAWTimeMSPrev, sensorEventRAWTimeMS );
  sensorEventRAWTimeMSPrev = sensorEventRAWTimeMS;

  if( deltaMS >= SENSOR_TIME_MIN_BT_UNITS ) {
    sensorScannerEventFlag = TRUE;
    sensorWeigherEventFlag = TRUE;

    currTUParam.barcode[0] = '\0';
    currTUParam.weight[0]  = '\0';

    printLog( "Sensor event, time delta = %ums\n", deltaMS );
  }
}

// Threads implementations
/**
  void *connScannerLoop( void *arg )
*/
void *connScannerLoop( void *arg )
{
  int *retVal = (int*) arg;
  int scannerClientFd = -1;
  struct sockaddr_in servAddr;
  uint16_t scannerPort = SCANNER_PORT_DEFAULT;

  printLog( "Starting %s\n", __func__ );
  setState( STATE_CONN_SCANNER, STATE_PARAM_UNKNOWN );

  scannerPort = uint16_t( atoi( stationConfig.scannerPort ));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons( scannerPort );

  if( inet_pton( AF_INET, stationConfig.scannerIPAddr, &servAddr.sin_addr ) <= 0 ) {
    printLog( "Error: Invalid address/ Address not supported, exit\n" );
    dwExit( EXIT_FAIL );
  }

  scannerConnected = FALSE; // volatile: need to initialize

  //  thread main loop
  while( TRUE ) {
    while( !scannerConnected ) {
     if(( scannerSocketFd = socket( AF_INET, SOCK_STREAM, 0 )) < 0 ) {
        printLog( "Error: Socket creation error, exit\n" );
        dwExit( EXIT_FAIL );
      }

      printLog( "Trying to connect to the the scanner [%s:%s]\n"
              , stationConfig.scannerIPAddr, stationConfig.scannerPort );

      scannerClientFd = connect( scannerSocketFd, (struct sockaddr*) &servAddr, sizeof( servAddr ));

      if( scannerClientFd < 0 ) {
        scannerConnected = FALSE;
        printLog( "Connection to scanner [%s:%s] failed: %s\n"
                , stationConfig.scannerIPAddr, stationConfig.scannerPort, strerror( errno ));
        shutdown( scannerSocketFd, SHUT_RDWR );
        close( scannerSocketFd );
        setState( STATE_CONN_SCANNER, STATE_PARAM_NOTOK );
        usleep( SLEEP_2S );
      } else {
        scannerConnected = TRUE;
        printLog( "Connection to scanner [%s:%s] OK\n"
                , stationConfig.scannerIPAddr, stationConfig.scannerPort );
        setNonblock( scannerSocketFd );
        int flag = 1; // allways 1
        if( setsockopt( scannerSocketFd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0 ) {
          printLog( "%s: setsockopt( TCP_NODELAY ) error: %s\n", strerror( errno ), __func__ );
        } else {
          printLog( "%s: setsockopt( TCP_NODELAY ) OK\n", __func__ );
        }

        setState( STATE_CONN_SCANNER, STATE_PARAM_OK );
      }
    }

    if( !running ) break;
    usleep( SLEEP_100MS );
  } // while( TRUE )

  setState( STATE_CONN_SCANNER, STATE_PARAM_UNKNOWN );

  printLog( "Thread %s finished, exit\n", __func__  );
  *retVal = EXIT_FAIL;
  return( NULL );
}

/**
  void *connWeigherLoop( void *arg )
*/
void *connWeigherLoop( void *arg )
{
  int *retVal = (int*) arg;
  int weigherClientFd = -1;
  struct sockaddr_in servAddr;
  uint16_t weigherPort = WEIGHER_PORT_DEFAULT;

  printLog( "Starting %s\n", __func__ );
  setState( STATE_CONN_WEIGHER, STATE_PARAM_UNKNOWN );

  weigherPort = uint16_t( atoi( stationConfig.weigherPort ));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons( weigherPort );

  if( inet_pton( AF_INET, stationConfig.weigherIPAddr, &servAddr.sin_addr ) <= 0 ) {
    printLog( "Error: Invalid address/ Address not supported, exit\n" );
    dwExit( EXIT_FAIL );
  }

  weigherConnected = FALSE; // volatile: need to initialize

  //  thread main loop
  while( TRUE ) {
    while( !weigherConnected ) {
     if(( weigherSocketFd = socket( AF_INET, SOCK_STREAM, 0 )) < 0 ) {
        printLog( "Error: Socket creation error, exit\n" );
        dwExit( EXIT_FAIL );
      }

      printLog( "Trying to connect to the the weigher [%s:%s]\n"
              , stationConfig.weigherIPAddr, stationConfig.weigherPort );

      weigherClientFd = connect( weigherSocketFd, (struct sockaddr*) &servAddr, sizeof( servAddr ));

      if( weigherClientFd < 0 ) {
        weigherConnected = FALSE;
        printLog( "Connection to weigher [%s:%s] failed: %s\n"
                , stationConfig.weigherIPAddr, stationConfig.weigherPort, strerror( errno ));
        shutdown( weigherSocketFd, SHUT_RDWR );
        close( weigherSocketFd );
        setState( STATE_CONN_WEIGHER, STATE_PARAM_NOTOK );
        usleep( SLEEP_2S );
      } else {
        weigherConnected = TRUE;
        printLog( "Connection to weigher [%s:%s] OK\n"
                , stationConfig.weigherIPAddr, stationConfig.weigherPort );
        setNonblock( weigherSocketFd );
        int flag = 1; // allways 1
        if( setsockopt( weigherSocketFd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0 ) {
          printLog( "%s: setsockopt( TCP_NODELAY ) error: %s\n", strerror( errno ), __func__ );
        } else {
          printLog( "%s: setsockopt( TCP_NODELAY ) OK\n", __func__ );
        }

        setState( STATE_CONN_WEIGHER, STATE_PARAM_OK );
      }
    }

    if( !running ) break;
    usleep( SLEEP_100MS );
  } // while( TRUE )

  setState( STATE_CONN_WEIGHER, STATE_PARAM_UNKNOWN );

  printLog( "Thread %s finished, exit\n", __func__  );
  *retVal = EXIT_FAIL;
  return( NULL );
}

/**
  void *scannerLoop( void *arg )
*/
void *scannerLoop( void *arg )
{
  int *retVal = (int*) arg;
  ssize_t nsend = 0;
  ssize_t nread = 0;
  uint16_t recvCnt = 0;
  char msg[SCANNER_SEND_MSG_SZ] = { 0 };
  char buf[SCANNER_RECV_BUF_SZ] = { 0 };

  printLog( "Starting %s\n", __func__ );

  pinSetup(); // setup GPIO pin

  while( TRUE ) {
    // check the receive buffer, it must be empty
    if( scannerConnected ) {
      if( recv( scannerSocketFd, buf, sizeof( buf ), MSG_PEEK ) > 0 ) {
        printLog( "%s: Receive buffer isn't empty\n", __func__ );
        flushRecvBuffer( scannerSocketFd, SCANNER_MAX_RECV_CNT );
      }
    }

    // sensor event
    if( sensorScannerEventFlag ) {
      sensorScannerEventFlag = FALSE;

      memset( buf, 0, sizeof( buf ));
      memset( currTUParam.barcode, 0, sizeof( currTUParam.barcode ));

      if( scannerConnected ) {
        strcpy( msg, SCANNER_START_TRIG_MSG );
        nsend = send( scannerSocketFd, msg, strlen( msg ), 0 );

        printLog( "%s: > Send: %d bytes [%s]\n", __func__, nsend, msg );

        if( nsend < 0 ) {
          printLog( "Send error: %s\n", strerror( errno ) );
          close( scannerSocketFd ); // closing the connected socket
          scannerConnected = FALSE; // trying to reconnect
          strcpy( buf, BARCODE_SEND_ERROR );
        } else {
          printLog( "%s: > Read\n", __func__ );
          recvCnt = 0;

          while(( nread = recv( scannerSocketFd, buf, sizeof( buf ), 0 )) < 0 ) {
            recvCnt++;
            if( recvCnt >= SCANNER_MAX_RECV_CNT ) break;
            usleep( SLEEP_10MS );
          }

          printLog( "%s: recvCnt = %u, nread = %zd\n", __func__, recvCnt, nread );

          if( nread < 0 ) {
            printLog( "%s: Read error: %s\n", __func__, strerror( errno ));
            close( scannerSocketFd ); // closing the connected socket
            scannerConnected = FALSE; // trying to reconnect
            strcpy( buf, BARCODE_DEFAULT );
          } else {
            // replace last terminating character with \0
            if( nread > 0 ) {
              if( isdigit( buf[nread - 1] ) ) {
                buf[nread] = '\0';
              } else {
                buf[nread - 1] = '\0';
              }
              printLog( "%s: nread = %d: [%s]\n", __func__, nread, buf );
            } else {
              strcpy( buf, BARCODE_DEFAULT );
            }
          }
        }
      } else {
        strcpy( buf, BARCODE_NOCONN );
      }

      printLog( "%s: Final barcode: [%s]\n", __func__, buf );

/*
      // print msg and hex
      for( size_t i = 0; buf[i]; i++ ) {
        fprintf( stderr, "%02X ", buf[i] );
      }
      fprintf( stderr, "\n" );
      // end of print
*/

      // filling barcode
      strcpy( currTUParam.barcode, buf );
    }

    if( !running ) break;
    usleep( SLEEP_1MS );
  } // while( TRUE )

  printLog( "Thread %s finished, exit\n", __func__  );
  *retVal = EXIT_FAIL;
  return( NULL );
}

/**
  void *weigherLoop( void *arg )
*/
void *weigherLoop( void *arg )
{
  int *retVal = (int*) arg;
  char buf[WEIGHER_RECV_BUF_SZ] = { 0 };
  size_t weightLen = 0;

  // MySQL
  int isSQLInited = FALSE;
  int isSQLConnected = FALSE;
  int insertOK = FALSE;
  // unsigned int weight = 0;
  char *currWeight = NULL;
  char dquery[DYN_QUERY_SZ] = { 0 };
  unsigned int checkSQLConnectionT0 = 0; // start time
  unsigned int checkSQLConnectionT1 = 0; // current time

  printLog( "Starting %s\n", __func__ );

  setState( STATE_CONN_DB, STATE_PARAM_UNKNOWN );

   // connecting to MySQL
  if(( SQLNewTUHandler = mysql_init( NULL )) == NULL ) {
    isSQLInited = FALSE;
    printLog( "SQL (%s): init error: %s\n", MYSQL_NEW_TU_LABEL, mysql_error( SQLNewTUHandler ));
  } else {
    isSQLInited = TRUE;
    printLog( "SQL (%s): init OK\n", MYSQL_NEW_TU_LABEL );
  }

  if( mysql_real_connect( SQLNewTUHandler, MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB, 0, NULL, 0 ) == NULL ) {
    isSQLConnected = FALSE;
    printLog( "SQL (%s): connect error: %s\n", MYSQL_NEW_TU_LABEL, mysql_error( SQLNewTUHandler ));
  } else {
    isSQLConnected = TRUE;
    printLog( "SQL (%s): connect OK\n", MYSQL_NEW_TU_LABEL );
  }

  // TODO: implement reconnection
  if( ! ( isSQLInited && isSQLConnected )) {
    printLog( "SQL (%s): error creating SQL connection, exit", MYSQL_NEW_TU_LABEL );
    setState( STATE_CONN_DB, STATE_PARAM_NOTOK );
    dwExit( EXIT_FAIL );
  } else {
    setState( STATE_CONN_DB, STATE_PARAM_OK );
  }

  while( TRUE ) {
    // check the receive buffer, it must be empty
    if( weigherConnected ) {
      if( recv( weigherSocketFd, buf, sizeof( buf ), MSG_PEEK ) > 0 ) {
        printLog( "%s: Receive buffer isn't empty\n", __func__ );
        flushRecvBuffer( weigherSocketFd, WEIGHER_MAX_RECV_CNT );
      }
    }

    // getting MySQL connection status and reconnecting if necessary
    checkSQLConnectionT1 = millis(); // set current time
    if( !isSQLConnected || ( getTimeDeltaMS( checkSQLConnectionT0, checkSQLConnectionT1 ) >= MYSQL_CHECK_CONN_INTERVAL_MS )) {
      isSQLConnected = checkSQLConnection( SQLNewTUHandler, MYSQL_NEW_TU_LABEL );

      checkSQLConnectionT0 = checkSQLConnectionT1; // Update start time
    }

    // sensor event
    if( sensorWeigherEventFlag ) {
      sensorWeigherEventFlag = FALSE;

      memset( currTUParam.weight, 0, sizeof( currTUParam.weight ));
      currWeight = getWeight( weigherSocketFd ); // getting weight

      if( currWeight ) {
        weightLen = strlen( currWeight );

        if( weightLen < WEIGHT_LEN ) {
          currWeight[weightLen] = '\0';
          strcpy( currTUParam.weight, currWeight );
        } else {
          strcpy( currTUParam.weight, WEIGHT_ERROR );
        }
      } else {
        strcpy( currTUParam.weight, WEIGHT_ERROR );
      }

      printLog( "%s: Current weight = [%s]\n", __func__, currTUParam.weight );

      // check current TU: barcode and weight isn't empty
      if(( currTUParam.barcode[0] != '\0' ) && ( currTUParam.weight[0] != '\0' )) {
        sprintf( dquery, "INSERT INTO T_dw00resp (barcode,weight) values ('%s','%s')", currTUParam.barcode, currTUParam.weight );
        printLog( "SQL (%s): [%s]\n", MYSQL_NEW_TU_LABEL, dquery );

        if( mysql_query( SQLNewTUHandler, dquery )) {
          insertOK = FALSE;
        } else {
          insertOK = TRUE;
        }

        if( insertOK ) {
          setState( STATE_CONN_DB, STATE_PARAM_OK );
          printLog( "SQL (%s): insert OK\n", MYSQL_NEW_TU_LABEL );
        } else {
          isSQLConnected = FALSE;
          setState( STATE_CONN_DB, STATE_PARAM_NOTOK );
          printLog( "SQL (%s): insert  error: %s\n", MYSQL_NEW_TU_LABEL, mysql_sqlstate( SQLNewTUHandler ));
        }
      }
    }

    if( !running ) break;
    usleep( SLEEP_1MS );
  } // while( TRUE )

  if( SQLNewTUHandler ) {
    mysql_close( SQLNewTUHandler );
    SQLNewTUHandler = NULL;
  }

  printLog( "Thread %s finished, exit\n", __func__  );
  *retVal = EXIT_FAIL;
  return( NULL );
}

/**
  void *checkDBLoop( void *arg )
*/
void *checkDBLoop( void *arg )
{
  int *retVal = (int*) arg;
  // MySQL
  MYSQL_RES *SQLSelectResult = NULL;
  MYSQL_ROW row = NULL;  // for current row parsing
  int isSQLInited = FALSE;
  int isSQLConnected = FALSE;
  int selectOK = FALSE;
  int updateOK = FALSE;
  int recordFound = FALSE;
  unsigned int weightRecordNumFields = 0;
  unsigned int checkSQLConnectionT0 = 0; // start time
  unsigned int checkSQLConnectionT1 = 0; // current time

  printLog( "Starting %s\n", __func__ );
  setState( STATE_CONN_DB, STATE_PARAM_UNKNOWN );

  // cURL + JSON
  char json[JSON_BUF_LEN] = { 0 };
  CURL *curl;
  CURLcode curlPerformResult;
  size_t jsonLen = 0;
  char userID[PREFIX_SZ + USER_ID_SZ] = { 0 };
  struct curl_slist *slist = NULL;
  long httpServerResponseCode = 0;

  curl_global_init( CURL_GLOBAL_NOTHING );

   // connecting to MySQL
  if(( SQLCheckDBHandler = mysql_init( NULL )) == NULL ) {
    printLog( "SQL (%s): init error: %s\n", MYSQL_CHECK_DB_LABEL, mysql_error( SQLCheckDBHandler ));
    isSQLInited = FALSE;
  } else {
    isSQLInited = TRUE;
    printLog( "SQL (%s): init OK\n", MYSQL_CHECK_DB_LABEL );
  }

  if( mysql_real_connect( SQLCheckDBHandler, MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB, 0, NULL, 0 ) == NULL ) {
    printLog( "SQL (%s): connect error: %s\n", MYSQL_CHECK_DB_LABEL, mysql_error( SQLCheckDBHandler ));
    isSQLConnected = FALSE;
  } else {
    isSQLConnected = TRUE;
    printLog( "SQL (%s): connect OK\n", MYSQL_CHECK_DB_LABEL );
  }

  // TODO: implement reconnection
  if( ! ( isSQLInited && isSQLConnected )) {
    printLog( "SQL (%s): error creating SQL connection, exit", MYSQL_CHECK_DB_LABEL );
    setState( STATE_CONN_DB, STATE_PARAM_NOTOK );
    dwExit( EXIT_FAIL );
  } else {
    setState( STATE_CONN_DB, STATE_PARAM_OK );
  }

  printLog( "Checking the DB for new records\n" );

  while( TRUE ) {
    // getting MySQL connection status and reconnecting if necessary
    checkSQLConnectionT1 = millis(); // set current time
    if( !isSQLConnected || ( getTimeDeltaMS( checkSQLConnectionT0, checkSQLConnectionT1 ) >= MYSQL_CHECK_CONN_INTERVAL_MS )) {
      isSQLConnected = checkSQLConnection( SQLCheckDBHandler, MYSQL_CHECK_DB_LABEL );

      checkSQLConnectionT0 = checkSQLConnectionT1; // Update start time
    }

    // First not processed weight record
    if( mysql_query( SQLCheckDBHandler, REQUEST_SELECT_NEW_TU )) {
      selectOK = FALSE;
    } else {
      selectOK = TRUE; // Don't log this result
    }

    if( selectOK ) {
      SQLSelectResult = mysql_store_result( SQLCheckDBHandler );

      if( SQLSelectResult == NULL ) {
        printLog( "SQL (%s): store result error: %s\n", MYSQL_CHECK_DB_LABEL, mysql_sqlstate( SQLCheckDBHandler ));
      }
    } else {
      isSQLConnected = FALSE;
      setState( STATE_CONN_DB, STATE_PARAM_NOTOK );
      printLog( "SQL (%s): select weight data error: %s\n", MYSQL_CHECK_DB_LABEL, mysql_sqlstate( SQLCheckDBHandler ));
    }

    if( SQLSelectResult ) {
      weightRecordNumFields = mysql_num_fields( SQLSelectResult );
      row = mysql_fetch_row( SQLSelectResult );

      if( row ) {
        recordFound = TRUE;
        printLog( "SQL (%s): new record found\n", MYSQL_CHECK_DB_LABEL );
        // TODO: else copy defaults
        if( row[UID_INDEX] )       strcpy( weightRecord.uid,       row[UID_INDEX] );
        if( row[BARCODE_INDEX] )   strcpy( weightRecord.barcode,   row[BARCODE_INDEX] );
        if( row[WEIGHT_INDEX] )    strcpy( weightRecord.weight,    row[WEIGHT_INDEX] );
        if( row[TIMESTAMP_INDEX] ) strcpy( weightRecord.timestamp, row[TIMESTAMP_INDEX] );
        if( row[STATUS_INDEX] )    strcpy( weightRecord.status,    row[STATUS_INDEX] );

        printLog( "SQL (%s): selected %d fields: [%s] [%s] [%s] [%s] [%s]\n"
                , MYSQL_CHECK_DB_LABEL
                , weightRecordNumFields, weightRecord.uid, weightRecord.barcode
                , weightRecord.weight, weightRecord.timestamp, weightRecord.status );
      } else {
        recordFound = FALSE;
      }

      mysql_free_result( SQLSelectResult );
    }
    // End of MySQL part

    // Curl part
    if( recordFound ) {
      jsonLen = 0;
      memset( userID, 0, sizeof( userID ));

      curl = curl_easy_init();
      if( curl == NULL ) {
        printLog( "Error: cURL init error, exit" );
        dwExit( EXIT_FAIL );
      }

      slist = curl_slist_append( NULL, "" );
      slist = curl_slist_append( slist, "Content-Type: application/json" );
      slist = curl_slist_append( slist, "Accept: application/json" );
      sprintf( userID, "user_id: %s", stationConfig.user_id );
      slist = curl_slist_append( slist, userID );
      slist = curl_slist_append( slist, "event_type: Weight_data" );

      /* set custom headers */
      curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist );
      curl_easy_setopt( curl, CURLOPT_URL, stationConfig.serverAddr );
      curl_easy_setopt( curl, CURLOPT_TIMEOUT, SERVER_COMPLETE_TIMEOUT );
      curl_easy_setopt( curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, SERVER_RESPONSE_TIMEOUT );
      curl_easy_setopt( curl, CURLOPT_USERNAME, stationConfig.login );
      curl_easy_setopt( curl, CURLOPT_PASSWORD, stationConfig.password );

      /* pass in a pointer to the data - libcurl does not copy */
      // Example: { "ArrayData": [ {"Date": "2023-09-25 09:51:38", "ТЕ": "000025179223", "Weight": "1345" } ] }
      sprintf( json, "{ \"ArrayData\": [ { \"Date\": \"%s\", \"TE\": \"%s\", \"Weight\": \"%s\" } ] }"
             , weightRecord.timestamp, weightRecord.barcode, weightRecord.weight );
      jsonLen = strlen( json );

      printLog( "JSON: %s\n", json );

      printLog( "Send %zu bytes to [%s]\n", jsonLen, stationConfig.serverAddr );
      curl_easy_setopt( curl, CURLOPT_POSTFIELDS, json );
      curlPerformResult = curl_easy_perform( curl );

      if( curlPerformResult == CURLE_OK ) {
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpServerResponseCode );
        printLog( "Server response code: [%lu]\n", httpServerResponseCode );

        if( httpServerResponseCode == HTTP_RESPONSE_CODE_OK ) {
          char dquery[DYN_QUERY_SZ] = { 0 };
          sprintf( dquery, "UPDATE T_dw00resp SET status='1' WHERE uid='%s'", weightRecord.uid );
          printLog( "SQL (check DB): [%s]\n", dquery );

          if( mysql_query( SQLCheckDBHandler, dquery )) {
            updateOK = FALSE;
          } else {
            updateOK = TRUE;
          }

          if( updateOK ) {
            printLog( "SQL (check DB): update OK\n" );
          } else {
            printLog( "SQL (check DB): update error: %s\n", mysql_sqlstate( SQLCheckDBHandler ));
          }
        }

        setState( STATE_CONN_HTTP, STATE_PARAM_OK );
      } else {
        printLog( "CURL perform error: %s, sleep for %us\n", strerror( errno ), SLEEP_2S / ONE_SECOND );
        setState( STATE_CONN_HTTP, STATE_PARAM_NOTOK );
        usleep( SLEEP_2S ); // sleep 2s
      }

      curl_slist_free_all( slist ); // ### may catch segfault
      curl_easy_cleanup( curl ); // always cleanup
    }

    if( !running ) break;
    usleep( CHECK_DB_DELAY ); // sleep 500ms
  } // while( TRUE )

  if( SQLCheckDBHandler ) {
    mysql_close( SQLCheckDBHandler );
    SQLCheckDBHandler = NULL;
  }

  curl_global_cleanup();

  setState( STATE_CONN_DB, STATE_PARAM_UNKNOWN );
  setState( STATE_CONN_HTTP, STATE_PARAM_UNKNOWN );

  printLog( "Thread %s finished, exit\n", __func__  );
  *retVal = EXIT_FAIL;
  return( NULL );
}

/**
  Message queue exchange thread
*/
void *msgQueueLoop( void *arg )
{
  int *retVal = (int*) arg;
  pthread_t id_ = pthread_self();
  int msgflg = IPC_CREAT | IPC_MASK;
  messageBuf rbuf;

  printLog( "Starting %s\n", __func__ );

  if( pthread_equal( id_, tid[THREAD_MSG_QUEUE] )) {
    if(( msqid = msgget( MQ_KEY, msgflg )) < 0 ) {
      perror("msgget");
      printLog( "Get a message queue identifier: ERROR!\n" );
    } else {
      printLog( "Get a message queue identifier: [%d] successfully\n", msqid );
    }

    // message receive loop
    while( TRUE ) {
      if( msgrcv( msqid, &rbuf, MSG_SZ, 1, IPC_NOWAIT | MSG_NOERROR ) < 0 ) {
        usleep( SLEEP_100MS );
      } else {
        printLog( "%s: Incoming command: [%s]\n", __func__, rbuf.mtext );
        cmdHandler( rbuf.mtext );
      }

      if( !running ) break;
      usleep( SLEEP_1MS );
    }
  }

  printLog( "Thread %s finished, exit\n", __func__ );
  *retVal = EXIT_FAIL;
  return NULL;
}

/**
*/
char *getWeight( int socket )
{
  // uint16_t retVal = 0;
  ssize_t nread = 0;
  uint16_t recvCnt = 0;
  char recvBuf[WEIGHER_RECV_BUF_SZ] = { 0 };
  char weight[WEIGHT_LEN] = { 0 };
  char rawWeight[WEIGHT_LEN] = { 0 };
  char *ptr = NULL;
  size_t weightLen = 0;
  unsigned int weightInt = 0;

  printLog( "%s: > Read\n", __func__ );
  recvCnt = 0;

  memset( recvBuf, 0, sizeof( recvBuf ));
  memset( weight, 0, sizeof( weight ));
  memset( rawWeight, 0, sizeof( rawWeight ));

  if( weigherConnected ) {
    while(( nread = recv( socket, recvBuf, sizeof( recvBuf ), 0 )) < 0 ) {
      recvCnt++;
      if( recvCnt >= WEIGHER_MAX_RECV_CNT ) break;
      usleep( SLEEP_10MS );
    }

    printLog( "%s: recvCnt = %u, nread = %zd\n", __func__, recvCnt, nread );

    if( nread < 0 ) {
      printLog( "%s: Read error: %s\n", __func__, strerror( errno ));
      close( socket ); // closing the connected socket
      weigherConnected = FALSE; // trying to reconnect
      strcpy( weight, WEIGHT_DEFAULT );
    } else {
      if( nread > 0 ) {
        recvBuf[nread] = '\0';
        printLog( "%s: nread = %d: [%s]\n", __func__, nread, recvBuf );

        // message from the weigher: STX + data + ETX
        if(( nread == WEIGHER_MSG_SZ ) &&  ( recvBuf[0] == CHAR_STX ) && ( recvBuf[nread-1] == CHAR_ETX )) {
          strncpy( rawWeight, &recvBuf[1], nread - 2 ); // data = nread - STX - ETX
          rawWeight[WEIGHT_BUF_SZ-1] = '\0';
          ptr = rawWeight;

          while( *ptr == '0' ) ptr++;

          weightLen = strlen( ptr );
          strncpy( weight, ptr, weightLen);

          weightInt = atoi( weight );
          weightInt += WEIGHT_CORRECTION_VALUE; // adding vorrection value // TODO: add to config
          sprintf( weight, "%u", weightInt );
          weightLen = strlen( weight );
          weight[weightLen] = '\0';
        } else {
          strcpy( weight, WEIGHT_WRONG_FORMAT );
        }
      } else {
        strcpy( weight, WEIGHT_DEFAULT );
      }
    }
  } else {
    strcpy( weight, WEIGHT_NOCONN );
  }

  ptr = weight;

  printLog( "%s: Final weight: [%s], ptr = [%s]\n", __func__, weight, ptr ); // ### FIXIT: remove print ptr

  return( ptr );
}

/**
  return:   EXIT_SUCCESS - connection is OK or established
            EXIT_FAIL - connection failed
*/
int checkSQLConnection( MYSQL *mysql, const char *label )
{
  int retVal = EXIT_SUCCESS;
  const char *stat = NULL;
  unsigned int error = 0;

  stat = mysql_stat( mysql );
  error = mysql_errno( mysql );

  if( stat == NULL ) {
    printLog( "%s: SQL (%s) stat error: %s\n", __func__, label ? label : "...", mysql_error( mysql ));
    if( !error ) error = TRUE;
  }

  if( error ) {
    if( stat ) printLog( "%s: SQL (%s): %s\n",  __func__, label ? label : "...", stat );

    printLog( "%s: SQL (%s): Trying to connect\n",  __func__, label ? label : "..." );

    if( mysql_real_connect( mysql, MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB, 0, NULL, 0 ) == NULL ) {
      retVal = EXIT_FAIL;
      printLog( "%s: SQL (%s): connect error: %s\n", __func__, label ? label : "...", mysql_error( mysql ));
      usleep( SLEEP_2S );
    } else {
      retVal = EXIT_SUCCESS;
      printLog( "%s: SQL (%s): connect OK\n", __func__, label ? label : "..." );
    }
  }

  return( retVal );
}
