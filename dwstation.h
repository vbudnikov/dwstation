#ifndef DWSTATION_H
#define DWSTATION_H

#include <stdint.h>
#include <unistd.h>
#include <mariadb/mysql.h>
#include <wiringPi.h>

#ifndef TRUE
#define TRUE                      1
#endif // TRUE

#ifndef FALSE
#define FALSE                     0
#endif // FALSE

#define DEBUG                     1
#define AWS_SUCCESS               0
#define AWS_FAIL                  1

#define THREAD_CONN_SCANNER       0
#define THREAD_CONN_WEIGHER       1
#define THREAD_SCANNER            2
#define THREAD_WEIGHER            3
#define THREAD_CHECK_DB           4
#define THREADS_NUM               5 // Check this when adding new fields

#define TIME_STR_SZ               32
#define PREFIX_SZ                 16

#define SERVER_ADDR_SZ            128
#define LOGIN_SZ                  32
#define PASSWORD_SZ               32
#define USER_ID_SZ                64
#define SCANNER_IP_ADDR_SZ        16
#define SCANNER_PORT_SZ           6
#define WEIGHER_IP_ADDR_SZ        16
#define WEIGHER_PORT_SZ           6
#define DYN_QUERY_SZ              256

// JSON
#define JSON_BUF_LEN              1024
#define UID_LEN                   16
#define BARCODE_LEN               24
#define WEIGHT_LEN                8
#define TIMESTAMP_LEN             32
#define STATUS_LEN                4

#define CFG_SERVER_ADDR_INDEX     0
#define CFG_LOGIN_INDEX           1
#define CFG_PASSWORD_INDEX        2
#define CFG_USER_ID_INDEX         3
#define CFG_SCANNER_IP_ADDR       4
#define CFG_SCANNER_PORT          5
#define CFG_WEIGHER_IP_ADDR       6
#define CFG_WEIGHER_PORT          7
#define CFG_LAST_INDEX            8 // Check this when adding new fields

#define UID_INDEX                 0
#define BARCODE_INDEX             1
#define WEIGHT_INDEX              2
#define TIMESTAMP_INDEX           3
#define STATUS_INDEX              4

#define T_CONFIG                  "T_dw00conf" // put in order
#define T_RESPONSE                "T_dw00resp" // put in order

#define SERVER_ADDR_DEFAULT       "http://127.0.0.1\0"
#define SERVER_RESPONSE_TIMEOUT   1
#define SERVER_COMPLETE_TIMEOUT   2
#define HTTP_RESPONSE_CODE_OK     405 // change to 200 // ###

#define PIN_DAT                   15 // GPIO14 => pin 8

#define SENSOR_EVENT_DELAY        ( 100 * 1000 ) // 100ms
#define SENSOR_TIME_MIN_BT_UNITS  1600 // ms

#define SCANNER_SEND_MSG_SZ       16
#define SCANNER_RECV_BUF_SZ       32
#define SCANNER_START_TRIG_MSG    "start\0"
#define SCANNER_STOP_TRIG_MSG     "stop\0"
#define SCANNER_PORT_DEFAULT      2001
#define SCANNER_MAX_RECV_CNT      300 // receive up to ... times
#define BARCODE_DEFAULT           "TimeOut\0"
#define BARCODE_NOCONN            "NoConn\0"

#define CHECK_DB_DELAY            ( 500 * 1000 ) // 500ms

#define ONE_SECOND                ( 1000 * 1000 )
#define SLEEP_1MS                 ( 1 * 1000 ) // 1 ms
#define SLEEP_5MS                 ( 5 * 1000 ) // 5 ms
#define SLEEP_10MS                ( 10 * 1000 ) // 10 ms
#define SLEEP_100MS               ( 100 * 1000 ) // 100 ms
#define SLEEP_1S                  ( 1 ) * ONE_SECOND // 1 s
#define SLEEP_2S                  ( 2 ) * ONE_SECOND // 2 s

#define LOG_FILE_FLUSH_INTERVAL   60 // seconds
#define LOG_FILE_NAME_SZ          256
#define LOG_BUF_SZ                512
#define SYS_CMD_SZ                128


extern const char *nowToday;
extern const char *appName;

extern int scannerSocketFd;
extern int weigherSocketFd;

struct stStationConfig {
  char serverAddr[SERVER_ADDR_SZ]        = { 0 };
  char login[LOGIN_SZ]                   = { 0 };
  char password[PASSWORD_SZ]             = { 0 };
  char user_id[USER_ID_SZ]               = { 0 };
  char scannerIPAddr[SCANNER_IP_ADDR_SZ] = { 0 };
  char scannerPort[SCANNER_PORT_SZ]      = { 0 };
  char weigherIPAddr[WEIGHER_IP_ADDR_SZ] = { 0 };
  char weigherPort[WEIGHER_PORT_SZ]      = { 0 };
};

struct stWeightRecord {
  char uid[UID_LEN]             = { 0 };
  char barcode[BARCODE_LEN]     = { 0 };
  char weight[WEIGHT_LEN]       = { 0 };
  char timestamp[TIMESTAMP_LEN] = { 0 };
  char status[STATUS_LEN]       = { 0 };
};

struct stCurrTUParam {
  char barcode[BARCODE_LEN] = { 0 };
  char weight[WEIGHT_LEN]   = { 0 };
};

extern struct stStationConfig stationConfig;
extern struct stWeightRecord  weightRecord;
extern struct stCurrTUParam   currTUParam;

extern MYSQL *SQLConnConfig;
extern MYSQL *SQLConnNewTU;
extern MYSQL *SQLConnCheckSend;

void handlersSetup( void );
void pinSetup( void );
void sensorEvent( void );
int flushRecvBuffer( int socket );

void *connScannerLoop( void *arg );
void *connWeigherLoop( void *arg );
void *scannerLoop( void *arg );
void *weigherLoop( void *arg );
void *checkDBLoop( void *arg );

void setNonblock( int socket );
uint16_t getWeightTest();

// aux
extern FILE *logFile;
extern volatile int logFileUpdated;

void printCurrTime( void );
int openLog( void );
int checkTimeLogReopen( void );
void printLog( const char *format, ... );
void flushLogFileBuffer( void );
unsigned int getTimeDeltaMS( unsigned int t0, unsigned int t1 );
void dwExit( int status );

#endif /* DWSTATION_H */
