#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <wiringPi.h>
#include <curl/curl.h>
#include <netinet/tcp.h>

#include "dwstation.h"

MYSQL *SQLConnNewTU = NULL;
MYSQL *SQLConnCheckSend = NULL;

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

/**
*/
void pinSetup( void )
{
  wiringPiSetup();
  pinMode( PIN_DAT, INPUT );

  // wiringPiISR( pin_dat, INT_EDGE_RISING, &sensorEvent );  // INT_EDGE_BOTH
  wiringPiISR( PIN_DAT, INT_EDGE_FALLING, &sensorEvent );  // ### changed to falling edge for refletive sensor

  printLog( "Using GPIO pin: PIN%d\n", PIN_DAT );

  // setting time values
  sensorEventRAWTime = time( NULL );
  sensorEventRAWTimePrev = sensorEventRAWTime;

  sensorEventRAWTimeMS = millis();
  sensorEventRAWTimeMSPrev = sensorEventRAWTimeMS;
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

  // fprintf( stderr, "Sensor event, time deltaMS = %ldms\n", deltaMS ); // DEBUG

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
  int scannerClientFd = -1;
  struct sockaddr_in serv_addr;
  uint16_t scannerPort = SCANNER_PORT_DEFAULT;

  scannerPort = uint16_t( atoi( stationConfig.scannerPort ));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons( scannerPort );

  if( inet_pton( AF_INET, stationConfig.scannerIPAddr, &serv_addr.sin_addr ) <= 0 ) {
    printLog( "Error: Invalid address/ Address not supported, exit\n" );
    exit( 1 );
  }

  //  thread main loop
  while( TRUE ) {
    while( !scannerConnected ) {
     if(( scannerSocketFd = socket( AF_INET, SOCK_STREAM, 0 )) < 0 ) {
        printLog( "Error: Socket creation error, exit\n" );
        exit( 1 );
      }

      printLog( "Trying to connect to the the scanner [%s:%s]\n", stationConfig.scannerIPAddr, stationConfig.scannerPort );
      // fprintf( stderr, "Trying to connect to the the scanner [%s:%s]\n", stationConfig.scannerIPAddr, stationConfig.scannerPort );

      scannerClientFd = connect( scannerSocketFd, (struct sockaddr*) &serv_addr, sizeof( serv_addr ));

      if( scannerClientFd < 0 ) {
        scannerConnected = FALSE;
        printLog( "Connection to scanner failed: %s\n", strerror( errno ));
        // fprintf( stderr, "Connection to scanner failed: %s\n", strerror( errno ));
        shutdown( scannerSocketFd, SHUT_RDWR ); // test: shutdown
        close( scannerSocketFd );
        usleep( SLEEP_2S );
      } else {
        scannerConnected = TRUE;
        printLog( "Connection to scanner OK\n" );
        // fprintf( stderr, "Connection to scanner OK\n" );
        setNonblock( scannerSocketFd );
        int flag = 1; // allways 1
        if( setsockopt( scannerSocketFd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0 ) {
          printLog( "setsockopt( TCP_NODELAY ) error: %s\n", strerror( errno ));
        } else {
          printLog( "setsockopt( TCP_NODELAY ) OK\n" );
        }
      }
    }

    usleep( SLEEP_100MS );
  }
}

/**
  void *connWeigherLoop( void *arg )
*/
void *connWeigherLoop( void *arg )
{
  while( TRUE ) {

    usleep( SLEEP_5MS );
  }
}

/**
  void *scannerLoop( void *arg )
*/
void *scannerLoop( void *arg )
{

  size_t nsend = 0;
  ssize_t nread = 0;
  uint16_t recvCnt = 0;

  char msg[SCANNER_SEND_MSG_SZ] = { 0 };
  char buf[SCANNER_RECV_BUF_SZ] = { 0 };

  pinSetup(); // setup GPIO pin

  while( TRUE ) {
    // check the receive buffer, it must be empty
    if( scannerConnected ) {
      if( recv( scannerSocketFd, buf, sizeof( buf ), MSG_PEEK ) > 0 ) {
        printLog( "Receive buffer isn't empty\n" );
        flushRecvBuffer( scannerSocketFd );
      }
    }

    if( sensorScannerEventFlag ) {
      sensorScannerEventFlag = FALSE;
      memset( buf, 0, sizeof( buf ));

      if( scannerConnected ) {
        strcpy( msg, SCANNER_START_TRIG_MSG );
        nsend = send( scannerSocketFd, msg, strlen( msg ), 0 );

        printLog( "> Send: %d bytes [%s]\n", nsend, msg );
        // fprintf( stderr, "> Send: %d bytes [%s]\n", nsend, msg );

        if( nsend < 0 ) {
          printLog( "Send error: %s\n", strerror( errno ) );
          close( scannerSocketFd ); // closing the connected socket
          break; // trying to reconnect
        } else {
          printLog( "> Read\n" );
          // fprintf( stderr, "> Read\n" );
          recvCnt = 0;

          // while(( nread = read( scannerSocketFd, buf, sizeof( buf ))) < 0 ) {
          while(( nread = recv( scannerSocketFd, buf, sizeof( buf ), 0 )) < 0 ) {
            recvCnt++;
            usleep( SLEEP_10MS );
            if( recvCnt >= SCANNER_MAX_RECV_CNT ) break;
          }
          // fprintf( stderr, "recvCnt = %d\n", recvCnt );

          if( nread < 0 ) {
            printLog( "Read error: %s\n", strerror( errno ));
            // fprintf( stderr, "Read error: %s\n", strerror( errno ));
            scannerConnected = FALSE;
            strcpy( buf, BARCODE_DEFAULT );
          } else {
            // replace last terminatinп character with \0
            if( nread > 0 ) {
              if( isdigit( buf[nread - 1] ) ) {
                buf[nread] = '\0';
              } else {
                buf[nread - 1] = '\0';
              }
              printLog( "nread = %d: [%s]\n", nread, buf );
              // fprintf( stderr, "nread = %d: [%s]\n", nread, buf );
            } else {
              strcpy( buf, BARCODE_DEFAULT );
            }
          }
        }
      } else {
        strcpy( buf, BARCODE_NOCONN );
      }

      // print msg and hex
      fprintf( stderr, "[%s]: ", buf );
      for( size_t i = 0; buf[i]; i++ ) {
        fprintf( stderr, "%02X ", buf[i] );
      }
      fprintf( stderr, "\n" );
      // end of print

      // filling barcode
      strcpy( currTUParam.barcode, buf );
    }

    usleep( SLEEP_1MS );
  } // while( TRUE )

  return NULL;
}

/**
  void *weigherLoop( void *arg )
*/
void *weigherLoop( void *arg )
{
  int SQLInited = 0;
  int SQLConnected = 0;
  int insertOK = 0;
  unsigned int weight = 0;

   // connecting to MySQL
  if(( SQLConnNewTU = mysql_init( NULL )) == NULL ) {
    printLog( "SQL (new TU): init error: %s\n", mysql_error( SQLConnNewTU ));
    SQLInited = FALSE;
  } else {
    SQLInited = TRUE;
    printLog( "SQL (new TU): init OK\n" );
  }

  if( mysql_real_connect( SQLConnNewTU, "127.0.0.1", "dwstation", "dwstation", "dwstation", 0, NULL, 0 ) == NULL ) {
    printLog( "SQL (new TU): connect error: %s\n", mysql_error( SQLConnNewTU ));
    SQLConnected = FALSE;
  } else {
    SQLConnected = TRUE;
    printLog( "SQL (new TU): connect OK\n" );
  }

  // TODO: implement reconnection
  if( ! ( SQLInited && SQLConnected )) {
    printLog( "SQL (new TU): error creating SQL connection, exit" );
    exit( 1 );
  }

  while( TRUE ) {
    if( sensorWeigherEventFlag ) {
      sensorWeigherEventFlag = 0;

      usleep( SLEEP_2S );
      weight = getWeightTest();
      // filling barcode
      sprintf( currTUParam.weight, "%u", weight );

      printLog( "Current weight = [%s]\n", currTUParam.weight );

      // check current TU: barcode and weight isn't empty
      if(( currTUParam.barcode[0] != '\0' ) && ( currTUParam.weight[0] != '\0' )) {
        char dquery[DYN_QUERY_SZ] = { 0 };
        sprintf( dquery, "INSERT INTO T_dw00resp (barcode,weight) values ('%s','%s')", currTUParam.barcode, currTUParam.weight );
        printLog( "SQL (new TU): [%s]\n", dquery );

        if( mysql_query( SQLConnNewTU, dquery )) {
          printLog( "SQL (new TU): insert  error: %s\n", mysql_sqlstate( SQLConnNewTU ));
          insertOK = FALSE;
        } else {
          insertOK = TRUE;
        }

        if( insertOK ) {
          printLog( "SQL (new TU): insert OK\n" );
        }
      }
    } // sensorWeigherEventFlag

    usleep( SLEEP_1MS );
  }

  if( SQLConnNewTU ) {
    mysql_close( SQLConnNewTU );
    SQLConnNewTU = NULL;
  }

  return NULL;
}

/**
  void *checkDBLoop( void *arg )
*/
void *checkDBLoop( void *arg )
{
  // MySQL
  MYSQL_RES *SQLSelectResult = NULL;
  MYSQL_ROW row = NULL;  // for current row parsing
  int SQLInited = 0;
  int SQLConnected = 0;
  int selectOK = 0;
  int updateOK = 0;
  int recordFound = 0;
  unsigned int weightRecordNumFields = 0;

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
  if(( SQLConnCheckSend = mysql_init( NULL )) == NULL ) {
    printLog( "SQL (check send): init error: %s\n", mysql_error( SQLConnCheckSend ));
    SQLInited = FALSE;
  } else {
    SQLInited = TRUE;
    printLog( "SQL (check send): init OK\n" );
  }

  if( mysql_real_connect( SQLConnCheckSend, "127.0.0.1", "dwstation", "dwstation", "dwstation", 0, NULL, 0 ) == NULL ) {
    printLog( "SQL (check send): connect error: %s\n", mysql_error( SQLConnCheckSend ));
    SQLConnected = FALSE;
  } else {
    SQLConnected = TRUE;
    printLog( "SQL (check send): connect OK\n" );
  }

  // TODO: implement reconnection
  if( ! ( SQLInited && SQLConnected )) {
    printLog( "SQL (check send): error creating SQL connection, exit" );
    exit( 1 );
  }

  printLog( "Checking the DB for new records\n" );

  while( TRUE ) {
    // Getting weight data
    if( SQLInited && SQLConnected ) {
      // First not processed weight record
      if( mysql_query( SQLConnCheckSend
                     , "SELECT uid,barcode,weight,timestamp,status FROM T_dw00resp"
                       " WHERE status=0 ORDER BY timestamp ASC limit 1" )) {
        selectOK = FALSE;
        printLog( "SQL (check send): select weight data error: %s\n", mysql_sqlstate( SQLConnCheckSend ));
      } else {
        selectOK = TRUE; // Don't log this result
      }

      if( selectOK ) {
        SQLSelectResult = mysql_store_result( SQLConnCheckSend );

        if( SQLSelectResult == NULL ) {
          printLog( "SQL (check send): store result error: %s\n", mysql_sqlstate( SQLConnCheckSend ));
        }
      }
    }

    if( SQLSelectResult ) {
      weightRecordNumFields = mysql_num_fields( SQLSelectResult );
      row = mysql_fetch_row( SQLSelectResult );

      if( row ) {
        recordFound = TRUE;

        if( row[UID_INDEX] )       strcpy( weightRecord.uid,       row[UID_INDEX] );
        if( row[BARCODE_INDEX] )   strcpy( weightRecord.barcode,   row[BARCODE_INDEX] );
        if( row[WEIGHT_INDEX] )    strcpy( weightRecord.weight,    row[WEIGHT_INDEX] );
        if( row[TIMESTAMP_INDEX] ) strcpy( weightRecord.timestamp, row[TIMESTAMP_INDEX] );
        if( row[STATUS_INDEX] )    strcpy( weightRecord.status,    row[STATUS_INDEX] );

        printLog( "SQL (check send): selected %d fields: [%s] [%s] [%s] [%s] [%s]\n"
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
      printLog( "SQL (check send): new record found\n" );

      jsonLen = 0;
      memset( userID, 0, sizeof( userID ));

      curl = curl_easy_init();
      if( curl == NULL ) {
        printLog( "Error: cURL init error, exit" );
        exit( 1 );
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
      sprintf( json, "{ \"ArrayData\": [ {\"Date\": \"%s\", \"TE\": \"%s\", \"Weight\": \"%s\"  } ] }"
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
          printLog( "SQL (check send): query to update: [%s]\n", dquery );

          if( mysql_query( SQLConnCheckSend, dquery )) {
            printLog( "SQL (check send): update error: %s\n", mysql_sqlstate( SQLConnCheckSend ));
            updateOK = FALSE;
          } else {
            updateOK = TRUE;
          }

          if( updateOK ) {
            printLog( "SQL (check send): update OK\n" );
          }
        }
      } else {
        printLog( "perform: %s\n", strerror( errno ));
        printLog( "res = %d\n", curlPerformResult );
      }

      curl_slist_free_all( slist ); // ### may catch segfault
      curl_easy_cleanup( curl ); // always cleanup
    }

    usleep( CHECK_DB_DELAY ); // sleep 500ms
  } // while( TRUE ) // ###

  if( SQLConnCheckSend ) {
    mysql_close( SQLConnCheckSend );
    SQLConnCheckSend = NULL;
  }

  curl_global_cleanup();

  return NULL;
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
uint16_t getWeightTest()
{
  uint16_t retVal = 0;

  retVal = ( 1000 * ( 1 + (uint16_t)( rand() % 10 ))) + (uint16_t)( rand() % 1000 );

  return( retVal );
}
