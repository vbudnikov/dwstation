#ifndef DWSTATION_H
#define DWSTATION_H

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <mariadb/mysql.h>
#include <wiringPi.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#ifndef TRUE
#define TRUE                      1
#endif // TRUE

#ifndef FALSE
#define FALSE                     0
#endif // FALSE

#define DEBUG                     FALSE
#define EXIT_SUCCESS              0
#define EXIT_FAIL                 1

#define THREAD_CONN_SCANNER       0
#define THREAD_CONN_WEIGHER       1
#define THREAD_SCANNER            2
#define THREAD_WEIGHER            3
#define THREAD_CHECK_DB           4
#define THREAD_MSG_QUEUE          5
#define THREADS_NUM               6 // Check this when adding new fields, should be the last

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
#define WEIGHT_LEN                16
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
#define CFG_LAST_INDEX            8 // Check this when adding new fields, should be the last

#define UID_INDEX                 0
#define BARCODE_INDEX             1
#define WEIGHT_INDEX              2
#define TIMESTAMP_INDEX           3
#define STATUS_INDEX              4

// MySQL
#define MYSQL_HOST                "localhost"
#define MYSQL_USER                "dwstation"
#define MYSQL_PASSWORD            "dwstation"
#define MYSQL_DB                  "dwstation"
#define T_CONFIG                  "T_dw00conf"
#define T_RESPONSE                "T_dw00resp"
#define MYSQL_CONFIG_LABEL        "config"
#define MYSQL_NEW_TU_LABEL        "new TU"
#define MYSQL_CHECK_DB_LABEL      "check DB"

// HTTP-server
#define SERVER_ADDR_DEFAULT       "http://127.0.0.1\0"
#define SERVER_RESPONSE_TIMEOUT   1
#define SERVER_COMPLETE_TIMEOUT   2
#define HTTP_RESPONSE_CODE_OK     200 // 405 // change to 200 // ###

#define PIN_DAT                   15 // GPIO14 => pin 8

#define SENSOR_EVENT_DELAY        ( 100 * 1000 ) // 100ms
#define SENSOR_TIME_MIN_BT_UNITS  1600 // ms

#define COMMON_RECV_BUF_SZ        256

#define SCANNER_SEND_MSG_SZ       16
#define SCANNER_RECV_BUF_SZ       32
#define SCANNER_START_TRIG_MSG    "start\0"
#define SCANNER_STOP_TRIG_MSG     "stop\0"
#define SCANNER_PORT_DEFAULT      2001
#define SCANNER_MAX_RECV_CNT      150 // receive up to ... times

#define BARCODE_DEFAULT           "TimeOut\0"
#define BARCODE_NOCONN            "NoConn\0"
#define BARCODE_SEND_ERROR        "SndErr\0"

#define CHAR_STX                  0x02
#define CHAR_ETX                  0x03
#define WEIGHER_RECV_BUF_SZ       128
#define WEIGHER_MSG_SZ            12
#define WEIGHT_BUF_SZ             11 // 10 weight characters + '\0'

#define WEIGHER_PORT_DEFAULT      3000
#define WEIGHER_MAX_RECV_CNT      200 // receive up to ... times

#define WEIGHT_DEFAULT            "TimeOut\0"
#define WEIGHT_NOCONN             "NoConn\0"
#define WEIGHT_WRONG_FORMAT       "WrFormat\0"
#define WEIGHT_ERROR              "Error\0"

#define CHECK_DB_DELAY            ( 500 * 1000 ) // 500ms

#define ONE_SECOND                ( 1000 * 1000 )
#define SLEEP_1MS                 ( 1 * 1000 ) // 1 ms
#define SLEEP_5MS                 ( 5 * 1000 ) // 5 ms
#define SLEEP_10MS                ( 10 * 1000 ) // 10 ms
#define SLEEP_100MS               ( 100 * 1000 ) // 100 ms
#define SLEEP_500MS               ( 500 * 1000 ) // 500 ms
#define SLEEP_1S                  ( 1 ) * ONE_SECOND // 1 s
#define SLEEP_2S                  ( 2 ) * ONE_SECOND // 2 s
#define SLEEP_2500MS              ( 2.5 ) * ONE_SECOND // 2500 ms

#define LOG_FILE_FLUSH_INTERVAL   60 // seconds
#define LOG_FILE_NAME_SZ          256
#define LOG_BUF_SZ                512
#define SYS_CMD_SZ                128

#define MSG_SZ                    128
#define MQ_KEY                    10
#define IPC_MASK                  0600
#define CMD_SEPARATOR             '='
#define CMD_SZ                    32
#define CMD_PARAM_SZ              16
#define CMD_RESTART_CMD           "restart"
#define CMD_RESTART_PARAM         "TRUE"
#define CMD_SHUTDOWN_CMD          "shutdown"
#define CMD_SHUTDOWN_PARAM        "TRUE"

#define STATE_BOOTING             0
#define STATE_STARTING            1
#define STATE_CONFIG              2
#define STATE_CONN_SCANNER        3
#define STATE_CONN_WEIGHER        4
#define STATE_CONN_DB             5
#define STATE_CONN_HTTP           6

#define STATE_PARAM_OK            0
#define STATE_PARAM_NOTOK         1
#define STATE_PARAM_UNKNOWN       2

#define STATE_CURR_TIME_REFRESH_MS   ( 10 * 1000 ) // 10s
#define MYSQL_CHECK_CONN_INTERVAL_MS ( 10 * 1000 ) // 10s

#define FILE_STATE_BOOTING        "/mnt/ramdisk/dwstation/state/booting.txt"     // "/mnt/ssd/dwstation/state/booting.txt"
#define FILE_STATE_STARTING       "/mnt/ramdisk/dwstation/state/starting.txt"    // "/mnt/ssd/dwstation/state/starting.txt"
#define FILE_STATE_CONFIG         "/mnt/ramdisk/dwstation/state/config.txt"      // "/mnt/ssd/dwstation/state/config.txt"
#define FILE_STATE_CONN_SCANNER   "/mnt/ramdisk/dwstation/state/connscanner.txt" // "/mnt/ssd/dwstation/state/connscanner.txt"
#define FILE_STATE_CONN_WEIGHER   "/mnt/ramdisk/dwstation/state/connweigher.txt" // "/mnt/ssd/dwstation/state/connweigher.txt"
#define FILE_STATE_CONN_DB        "/mnt/ramdisk/dwstation/state/conndb.txt"      // "/mnt/ssd/dwstation/state/conndb.txt"
#define FILE_STATE_CONN_HTTP      "/mnt/ramdisk/dwstation/state/connhttp.txt"    // "/mnt/ssd/dwstation/state/connhttp.txt"
#define FILE_STATE_CURR_TIME      "/mnt/ramdisk/dwstation/state/currtime.txt"

extern volatile int running;
extern const char *nowToday;
extern const char *appName;

extern int scannerSocketFd;
extern int weigherSocketFd;
extern pthread_t tid[THREADS_NUM];

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

extern MYSQL *SQLConfigHandler;
extern MYSQL *SQLNewTUHandler;
extern MYSQL *SQLCheckDBHandler;

void handlersSetup( void );
void pinSetup( void );
void sensorEvent( void );
int flushRecvBuffer( int socket, uint16_t cnt );

void *connScannerLoop( void *arg ); // Connection to the scanner handler
void *connWeigherLoop( void *arg ); // Connection to the weigher handler
void *scannerLoop( void *arg );
void *weigherLoop( void *arg );
void *checkDBLoop( void *arg );
void *msgQueueLoop( void *arg );

void setNonblock( int socket );
char *getWeight( int socket );
int checkSQLConnection( MYSQL *mysq, const char *label );

// aux
extern FILE *logFile;
extern volatile int logFileUpdated;

void printCurrTime( void );
int openLog( void );
int checkTimeLogReopen( void );
void printLog( const char *format, ... );
void flushLogFileBuffer( void );
unsigned int getTimeDeltaMS( unsigned int t0, unsigned int t1 );
void dwExit( int status ); // TODO:
int cmdHandler( char *data );
void calcelAllThreads( void );
void closeAllSockets( void );
void closeAllSQLConnections( void );
int setState( int state, int param );
int setStateCurrTime();

#endif /* DWSTATION_H */
