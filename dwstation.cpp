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

#include "dwstation.h"

int scannerSocketFd = -1;
int weigherSocketFd = -1;

static time_t sensorEventRAWTime = 0;
static time_t sensorEventRAWTimePrev = 0;

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
}

/**
*/
void sensorEvent( void )
{
  int pinData = 0;
  time_t delta = 0;

  usleep( SENSOR_EVENT_DELAY );

  sensorEventRAWTime = time( NULL );
  pinData = digitalRead( PIN_DAT );

  if( pinData == 1 ) {
    return; // bad interrupt
  }

  delta = sensorEventRAWTime - sensorEventRAWTimePrev;
  sensorEventRAWTimePrev = sensorEventRAWTime;

  if( delta >= SENSOR_TIME_MIN_BT_UNITS ) {
    sensorScannerEventFlag = TRUE;
    sensorWeigherEventFlag = TRUE;

    currTUParam.barcode[0] = '\0';
    currTUParam.weight[0]  = '\0';

    printLog( "Sensor event, time delta %lds\n", delta );
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
      fprintf( stderr, "Trying to connect to the the scanner [%s:%s]\n", stationConfig.scannerIPAddr, stationConfig.scannerPort );

      scannerClientFd = connect( scannerSocketFd, (struct sockaddr*) &serv_addr, sizeof( serv_addr ));

      if( scannerClientFd < 0 ) {
        scannerConnected = FALSE;
        printLog( "Connection to scanner failed: %s\n", strerror( errno ));
        fprintf( stderr, "Connection to scanner failed: %s\n", strerror( errno ));
        shutdown( scannerSocketFd, SHUT_RDWR ); // test: shutdown
        close( scannerSocketFd );
        usleep( SLEEP_2S );
      } else {
        scannerConnected = TRUE;
        printLog( "Connection to scanner OK\n" );
        fprintf( stderr, "Connection to scanner OK\n" );
        setNonblock( scannerSocketFd );
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

  pinSetup();   // setup GPIO pin

  while( TRUE ) {
    // check the receive buffer, it must be empty
    if( scannerConnected ) {
      if( recv( scannerSocketFd, buf, sizeof( buf ), MSG_PEEK ) > 0 ) {
        fprintf( stderr, "recv buffer isn't empty\n" );
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
        fprintf( stderr, "> Send: %d bytes [%s]\n", nsend, msg );

        if( nsend < 0 ) {
          printLog( "Send error: %s\n", strerror( errno ) );
          close( scannerSocketFd ); // closing the connected socket
          break; // trying to reconnect
        } else {
          printLog( "> Read\n" );
          fprintf( stderr, "> Read\n" );
          recvCnt = 0;

          // while(( nread = read( scannerSocketFd, buf, sizeof( buf ))) < 0 ) {
          while(( nread = recv( scannerSocketFd, buf, sizeof( buf ), 0 )) < 0 ) {
            recvCnt++;
            usleep( SLEEP_10MS );
            if( recvCnt >= SCANNER_MAX_RECV_CNT ) break;
          }
          fprintf( stderr, "recvCnt = %d\n", recvCnt );

          if( nread < 0 ) {
            printLog( "Read error: %s\n", strerror( errno ));
            fprintf( stderr, "Read error: %s\n", strerror( errno ));
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
              fprintf( stderr, "nread = %d: [%s]\n", nread, buf );
            } else {
              strcpy( buf, BARCODE_DEFAULT );
            }
          }
        }
      } else { // scannerConnected
        strcpy( buf, BARCODE_NOCONN );
      } // !scannerConnected


      // print hex
      for( size_t i = 0; buf[i]; i++ ) {
        fprintf( stderr, "%02X ", buf[i] );
      }
      fprintf( stderr, "\n" );
      // end of print hex

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
  unsigned int weight = 0;
  int isInsertOK = 0;

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
        printLog( "New TU: [%s]\n", dquery );

        if( mysql_query( SQLCfgConn, dquery )) {
          printLog( "INSERT query error: %s\n", mysql_sqlstate( SQLCfgConn ));
          isInsertOK = FALSE;
        } else {
          isInsertOK = TRUE;
        }

        if( isInsertOK ) {
          printLog( "INSERT query OK\n" );
        }
      }
    } // sensorWeigherEventFlag

    usleep( SLEEP_1MS );
  }

  return NULL;
}

/**
  void *checkDBLoop( void *arg )
*/
void *checkDBLoop( void *arg )
{
  // MySQL
  int isSelectOK = 0;
  int isUpdateOK = 0;
  int isRecordFound = 0;
  MYSQL_ROW row = NULL;
  unsigned int numFields = 0;

  // cURL + JSON
  char json[JSON_BUF_LEN] = { 0 };
  CURL *curl;
  CURLcode curlPerformResult;
  long httpServerResponseCode = 0;

  printLog( "Checking the DB for new records\n" );

  while( TRUE ) {
    // Getting weight data
    if( isSQLInited && isSQLConnected && isCfgOK ) {
      // First not processed weight record
      if( mysql_query( SQLCfgConn, "SELECT uid,barcode,weight,timestamp,status FROM T_dw00resp WHERE status=0 ORDER BY timestamp ASC limit 1" )) {
        isSelectOK = FALSE;
        printLog( "Weight data: SELECT query error: %s\n", mysql_sqlstate( SQLCfgConn ));
      } else {
        isSelectOK = TRUE;
      }

      if( isSelectOK ) {
        SQLSelectResult = mysql_store_result( SQLCfgConn );
        if( SQLSelectResult == NULL ) {
          printLog( "SLQ store result error: %s\n", mysql_sqlstate( SQLCfgConn ));
        }
      }
    }

    if( SQLSelectResult ) {
      numFields = mysql_num_fields( SQLSelectResult );

      row = mysql_fetch_row( SQLSelectResult );
      if( row ) {
        isRecordFound = TRUE;

        if( row[UID_INDEX] )       strcpy( weightRecord.uid,       row[UID_INDEX] );
        if( row[BARCODE_INDEX] )   strcpy( weightRecord.barcode,   row[BARCODE_INDEX] );
        if( row[WEIGHT_INDEX] )    strcpy( weightRecord.weight,    row[WEIGHT_INDEX] );
        if( row[TIMESTAMP_INDEX] ) strcpy( weightRecord.timestamp, row[TIMESTAMP_INDEX] );
        if( row[STATUS_INDEX] )    strcpy( weightRecord.status,    row[STATUS_INDEX] );

        printLog( "Selected %d fields: [%s] [%s] [%s] [%s] [%s]\n"
                , numFields, weightRecord.uid, weightRecord.barcode, weightRecord.weight, weightRecord.timestamp, weightRecord.status );
      } else {
        isRecordFound = FALSE;
        // if( DEBUG ) { printCurrTime(); fprintf( stderr, "No records found\n" ); } // ###
      }
    }
    // End of MySQL part

    // Curl part
    if( isRecordFound ) {
      size_t n = 0;
      char user_id_str[PREFIX_SZ + USER_ID_SZ] = { 0 };
      struct curl_slist *slist1 = NULL;

      curl = curl_easy_init();
      if( curl == NULL ) {
        printLog( "Error: cURL init error, exit" );
        exit( 1 );
      }

      slist1 = curl_slist_append( slist1, "Content-Type: application/json" );
      slist1 = curl_slist_append( slist1, "Accept: application/json" );
      sprintf( user_id_str, "user_id: %s", stationConfig.user_id );
      slist1 = curl_slist_append( slist1, user_id_str );
      slist1 = curl_slist_append( slist1, "event_type: Weight_data" );

      /* set custom headers */
      curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist1 );
      curl_easy_setopt( curl, CURLOPT_URL, stationConfig.serverAddr );
      curl_easy_setopt( curl, CURLOPT_TIMEOUT, SERVER_COMPLETE_TIMEOUT );
      curl_easy_setopt( curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, SERVER_RESPONSE_TIMEOUT );
      curl_easy_setopt( curl, CURLOPT_USERNAME, stationConfig.login );
      curl_easy_setopt( curl, CURLOPT_PASSWORD, stationConfig.password );

      /* pass in a pointer to the data - libcurl does not copy */
      // Example: { "ArrayData": [ {"Date": "2023-09-25 09:51:38", "ТЕ": "000025179223", "Weight": "1345" } ] }
      sprintf( json, "{ \"ArrayData\": [ {\"Date\": \"%s\", \"TE\": \"%s\", \"Weight\": \"%s\"  } ] }"
             , weightRecord.timestamp, weightRecord.barcode, weightRecord.weight );
      n = strlen( json );

      printLog( "Send %zu bytes to [%s]\n", n, stationConfig.serverAddr );
      curl_easy_setopt( curl, CURLOPT_POSTFIELDS, json );
      curlPerformResult = curl_easy_perform( curl );

      if( curlPerformResult == CURLE_OK ) {
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpServerResponseCode );
        printLog( "Server response code: [%lu]\n", httpServerResponseCode );

        if( httpServerResponseCode == HTTP_RESPONSE_CODE_OK ) {
          char dquery[DYN_QUERY_SZ] = { 0 };
          sprintf( dquery, "UPDATE T_dw00resp SET status='1' WHERE uid='%s'", row[0] );
          printLog( "Query to update: [%s]\n", dquery );

          if( mysql_query( SQLCfgConn, dquery )) {
            printLog( "UPDATE query error: %s\n", mysql_sqlstate( SQLCfgConn ));
            isUpdateOK = FALSE;
          } else {
            isUpdateOK = TRUE;
          }

          if( isUpdateOK ) {
            printLog( "UPDATE query OK\n" );
          }
        }
      } else {
        printLog( "perform: %s\n", strerror( errno ));
        printLog( "res = %d\n", curlPerformResult );
      }

      curl_easy_cleanup( curl ); // always cleanup
    }

    usleep( CHECK_DB_DELAY ); // sleep 500ms
  } // while( TRUE ) // ###

  if( SQLCfgConn ) mysql_close( SQLCfgConn );
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
unsigned int getWeightTest()
{
  unsigned int retVal = 0;

  retVal = ( 1000 * ( 1 + (unsigned int )( rand() % 9))) + (unsigned int)( rand() % 1000 );
  printLog( "dummy weight = [%u]\n", retVal );

  return( retVal );
}

/**
*/
int flushRecvBuffer( int socket )
{
  uint16_t recvCnt = 0;
  ssize_t nread = -1;
  char buf[SCANNER_RECV_BUF_SZ] = { 0 };

  fprintf( stderr, "Flushing recv buffer\n" );
  while(( nread = recv( socket, buf, sizeof( buf ), 0 )) < 0 ) {
    recvCnt++;
    usleep( SLEEP_10MS );
    if( recvCnt >= SCANNER_MAX_RECV_CNT ) break;
  }

  return( 0 );
}

