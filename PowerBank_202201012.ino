//2022.10.12 수정
// http://211.253.29.135/
#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include <LiquidCrystal.h>
#include <MsTimer2.h>
#include <math.h>
#include <string.h>
#include <Arduino.h>

const bool IS_DEVELOP = true; //개발모드 일 경우 true,

#define MAX_VOLTAGE 5460  //전압 최대값 (52V -> 54.6V로 수정)
#define MIN_VOLTAGE 3510  //전압 최소값 (39V -> 35.1V로 수정)

//바이크 번호 정의
#define MAX_BIKE_NUMBER_LENGTH 4
const char BIKE_NUMBER[MAX_BIKE_NUMBER_LENGTH+1] = "0020"; //바이크 번호 char형 4자리 넣기 -> 현재 바꿔놓았음
const int BIKE_NUMBER_INT = String(BIKE_NUMBER).toInt();   //바이크 번호 int형으로 바꾼것도 가지고 있기

LiquidCrystal g_lcd(31, 32, 33, 34, 35, 36);  //lcd 생성
Adafruit_ADS1115 g_ads;                       //아날로그값 측정


/*
  시간관련 정의
*/
typedef struct Time{
  const int REFRESH = 500;                    //ms단위로 작성, 500ms당 1번 새로고침
  const int TICK = 1000/REFRESH;              //1초당 몇 번 새로고침 되는지

  const int INTERVAL_USE = TICK*10;           //사용중일 때 주기, TICK * (sec) -> 현재는 10초
  // const int INTERVAL_NOT_USE = TICK*60*60*3;  //사용안할 때 주기, 현재는 3시간
  const int INTERVAL_NOT_USE = TICK*10;       //임시로 10초로 해두었음
  const int RESPONSE = TICK*10;                //회신시간, 지정된 시간안에 회신이 안 오면 다시 보냄
  const int CHECK = TICK*30;                  //세션 체크 주기,

  int current = 0;                            //현재 시간
  int socket_connect = 0;                     //소켓 연결시간 체크
  int socket_check = 0;                       //소켓 체크 시간
  int on_time = 0;                            //정시 데이터 보내는 시각
}Time;

Time g_time;  //Time 구조체 생성

/*
  프로토콜 데이터 길이 정의
*/

typedef struct Length{
  const int BB_OK_RECEIVE = (2+4+2+2+2);        //BB????OK??FF, 데이터 받을 때 데이터의 길이
  const int BB_RECEIVE = (2+4+2+2);             //BB????PPFF, 또는 BB????SSFF, 데이터 받을 때 데이터의 길이

  const int AA_SEND = (2+4+2+4+8+2+4+8+8+2+2+2);  //AA데이터 보낼때 길이 정의
//  const int AA_SEND = (2+4+2+4+8+2+4+8+8+2+2);  //FF뺀 임시 AA데이터 보낼때 길이 정의
  
  const int EE_SEND = (2+4+2+4+8+2+4+8+8+2+2+2);  //EE데이터 보낼때 길이 정의, 현재는 AA와 동일
  const int SS_SEND = (2+4+2+2);                //SS데이터 보낼때 길이 정의

  const int BB_OK_SEND = (2+4+2+2+2);           //BB????OK??FF보낼때 길이 정의
}Length;

Length g_length;  //Length구조체 생성

/*
  PIN관련 정의
*/

typedef struct PIN{ //pin번호 셋팅
  const int VIBRATE = 2;      //진동센서

  const int POWER = 11;       //바이크 전원

  const int LTE = 41;         //lte통신
  const int LTE_RESET = 18;   //lte리셋시 필요

  const int MODE_CHANGE = 27; //모드변경
  const int MODE_UP = 28;     //증가 스위치
  const int MODE_DOWN = 29;   //감소 스위치
  
  const int CLCD = 37;        //CLCD

  const int PWM_BASE = 43;    //PWM base번호, 추후 PWM_BASE + 1 -> PWM1이 되는 식
  const int PWM1 = 44;        //PWM1번, 출력 전압 조정(12V)
  const int PWM2 = 45;        //PWM2번, 출력 전압 조정(26V)
  const int PWM3 = 46;        //PWM3번, 출력 전압 조정(38V)
  const int PWM4 = 47;        //PWM4번, 출력 전압 조정(45V)
  const int PWM5 = 48;        //PWM5번, 출력 전압 조정(55V)
}PIN;

PIN g_pin;  //pin구조체 생성

/*
  Socket관련 변수 및 함수 지정
*/
typedef struct Socket{
  String SERVER_URL = "115.85.181.24";  //전송 받을 서버의 IP주소
  const int SERVER_PORT = 5000;         //전송 받을 서버의 포트번호
  const int SOCKET_NUMBER = 0;          //소켓 번호 지정, 번호같은 경우 크게 상관없음

  bool is_started = false;              //소켓 절차가 시작했는지
  bool is_created = false;              //생성이 되었는지
  bool is_connected = false;            //연결이 제대로 수립되었는지
  bool is_success = false;              //서버와의 연결(BB0001CCFF)이 완료되었는지
  bool is_first_connected = false;      //소켓이 제대로 연결된 후 처음인지
  bool is_disconnected = false;         //소켓 연결이 헤재된 경우

  bool is_ss_cc_sended = false;         //접속 시작시 데이터 전송을 보냈는지

  bool is_pp_received = false;          //BB????PPFF를 받았는지, 사용승인 요청
  bool is_ss_received = false;          //BB????SSFF를 받았는지, 사용해제 요청
} Socket;

Socket g_socket;  //Socket 구조체 생성

void CreateSocket();                                    //소켓 생성
void ConnectSocket();                                   //소켓 연결
void Send(const char data[], int data_length);          //소켓으로 데이터 전송
void CheckSocket();                                     //소켓 연결상태 확인

/*
  bike관련 정의
*/
typedef struct Bike{
  int voltage;        //전압값 저장
  int current;        //전류값 저장
  int temperature;    //온도값 저장
  int soc;            //soc값 저장
  int vibrate;        //진동수 저장

  int bike_mode = 1;  //처음에는 1로 시작 (55V)

  const int IS_NOT_USE = 0;   //바이크가 사용중인 아닐 경우
  const int IS_USE = 1;       //바이크가 사용중인 경우

  bool is_available = false;  //바이크가 사용가능한지, 처음에는 false로
  bool is_use = IS_NOT_USE;   //바이크가 사용중인지
  bool is_emergency = false;  //긴급 상황인지
}Bike;

Bike g_bike;  //Bike 구조체 생성


/*
  LTE관련 정의
*/
int AvailableLTEData(); //LTE에 들어온 데이터가 있는지 확인
String ReadLTEData();     //LTE Data를 읽어 1byte씩 반환

/*
  수신관련 Parsing관련 정의
*/
#define RECEIVED_NOT_DEFINED                0    //정의되지 않은 명령어

#define RECEIVED_SUCCESS_NETWORK_CONNECTION 11   //`*WIND:11,INSRV`, 네트워크 접속이 성공한 경우 (11,INSRV)
#define RECEIVED_DISABLE_NETWORK            12   //`*WIND:12,OOS`, 서비스 사용 불가 (12,OOS)

#define RECEIVED_DISCONNECTED_FROM_SERVER   99   //`+WSOEVT:<session_id>,DIS_IND`, 서버에서 연결을 종료한 경우

#define RECEIVED_GPS                        100  //`$$GPS`로 시작하는 GPS데이터 일 경우

#define RECEIVED_OKAA                       101  //`BB????OKAAFF`, AA데이터 보내고 난 후 서버의 응답
#define RECEIVED_OKEE                       102  //`BB????OKEEFF`, EE데이터 보내고 난 후 서버의 응답
#define RECEIVED_OKCC                       103  //`BB????OKCCFF`, 접속 시작시 데이터 전송 후 서버의 응답
#define RECEIVED_OKDD                       104  //`BB????OKDDFF`, 접속 종료시 데이터 전송 후 서버의 응답

#define RECEIVED_PP                         105  //`BB????PPFF`, 바이크 사용승인 요청을 서버로부터 받은 경우
#define RECEIVED_SS                         106  //`BB????SSFF`, 바이크 사용해제 요청을 서버로부터 받은 경우

#define RECEIVED_SOCKET_CHECK               201  //+WSOSS??????, 소켓의 상태를 조회한 결과가 온 경우

#define RECEIVED_SOCKET_CREATE_FAIL         301  //+WSOCR:0
#define RECEIVED_SOCKET_CREATE_SUCCESS      302  //+WSOCR:1
#define RECEIVED_SESSION_WAIT               303  //+WSOCO:1
#define RECEIVED_SESSION_COMPLETE           304  //+WSOCO:2
#define RECEIVED_SESSION_CLOSED             305  //+WSOCL:?,CLOSE_CMPL

int g_current_received = RECEIVED_NOT_DEFINED;  //일단 정의되지 않은 명령어로 초기화
char g_WSOCL[100];
char g_DIS_IND[100];
char g_WSOCO[100];
char g_WSOCM[100];


String g_parsing_data;
bool g_parsing_done = false;

const char END_MARKER = '\n'; //줄바꿈을 기준으로 LTE데이터 구분

void ParsingLTEData();                                     //LTE데이터 Buffer에서 데이터를 읽어 END_MARKER를 기준으로 Parsing하여 g_parsing_data에 저장
bool CompareData(const char target[], int target_length, int end);  //Parsing된 주어진 문자열과 비교
void CheckReceiveCommand();                                //Parsing된 데이터를 읽어 명령어를 해석합니다


/*
  송신관련 정의
*/
#define SENDING_NOT_DEFINED                    0  //정의되지 않은 명령어

#define SENDING_AA_USE                         11 //AA0001~, 사용 시 정시데이터
#define SENDING_AA_NOT_USE                     12 //AA0001~, 미사용 시 정시데이터
#define SENDING_AA_USE_RESEND                  13 //AA0001~, 사용 시 정시데이터 다시보내기
#define SENDING_AA_NOT_USE_RESEND              14 //AA0001~, 사용 시 정시데이터 다시보내기

#define SENDING_EE                             21 //EE0001~, 긴급 데이터
#define SENDING_EE_RESEND                      22 //EE0001~, 긴급 데이터 다시 보내기

#define SENDING_OK_PP                          31 //BB0001PPFF, 바이크 사용승인 받을 경우
#define SENDING_OK_SS                          32 //BB0001SSFF, 바이크 사용해제 받을 경우

#define SENDING_SS_CC                          41 //SS0001CCFF, 접속 시작시 데이터 전송
#define SENDING_SS_DD                          42 //SS0001DDFF, 접속 종료시 데이터 전송
#define SENDING_SS_CC_RESEND                   43 //SS0001CCFF, 접속 시작시 데이터 재전송
#define SENDING_SS_DD_RESEND                   44 //SS0001DDFF, 접속 종료시 데이터 재전송

int g_current_sending = SENDING_NOT_DEFINED; //일단 정의되지 않은 명령어로 초기화
void CheckSendingCommand();                  //어떤 것을 보낼 차례인지 확인
bool IsEmergency();                          //긴급 데이터를 보낼 차례인지 확인


/*
  GPS관련 정의
  https://github.com/Wiznet/woorinet-wd-n400s-kr/blob/main/ATCommand_WD_N400S_GPS.md
  https://os.mbed.com/users/AustinKim/code/WIZnet-IoTShield-WM-N400MSE-GPS//file/3fed3b61771e/main.cpp/
*/
typedef struct GPS{
  int date;             // yyyymmdd  날짜
  int utc;              // hhmmss    시간
  long latitude = 0; // dddddddd -> 자리수는 7자리 일수도 8자리 일수도 있음,  36.12345 이면은 3612345, 365.12345 이면은 36512345임
  long longitude = 0;// dddddddd
  int altitude = 0; // ddd -> 7.5m 이면 750 인듯?
  int kmPerH=0;     // xxx.x 속력
  int direct=0;         // 0~359, 방향
  int hDop=0;           // 13이면 1.3이라는 뜻, 소수점 1자리까지 표시, 여기서는 안 쓸 예정
  String reliability=String('V');     //'A' 이거나 'V'임, 정상이면 'A' 비정상이면 'V'
  // int lte;           // 0~2의 int, 0: No Service, 1: Limited Service, 2: Service Available
  // int emm;           // EMM reject, 무선 네트워크 연결 실패, 255면 정상
  // int esm;           // ESM reject, 무선 네트워크 연결 실패2, 255면 정상
  // int ss;            // signal strength, 무선 네트워크 신소 세기, 음의 정수로 표시됨  
  // int num_sattle;    // number of satellites 0-12, 위성 개수
  // char satinfo1[8];  // satellites infomation ID-signal strength
  // char satinfo2[8];  // satellites infomation ID-signal strength
  // char satinfo3[8];  // satellites infomation ID-signal strength
}GPS;

GPS g_gps;          //GPS 구조체 생성

void ActivateGPS();       //GPS기능 활성화
void ConfigurateGPS();    //GPS 설정
bool getGPSInformation(); //GPS정보를 받으면 Parsing하여서 g_gps객체에 넣음

/*
  Setup관련 함수 선언
*/
void LTESetup();      //LTE 모듈 셋업 함수
void TimerSetup();    //Timer 셋업 함수
void CLCDSetup();     //CLCD 셋업 함수
void PowerSetup();    //전원 셋업 함수
void PowerOn();       //전원 켜기
void PowerOff();      //전원 끄기
void BikeModeSetup(); //바이크 모드 셋업 함수
void PWMSetup();      //PWM 셋업 함수

void ChangeBikeMode(int mode);  //mode를 입력받아서 전압의 출력을 변경, mode: 1~5
void PressButton();             //스위치를 누르면 작동
                                // MODE_CHANGE
                                // MODE_UP
                                // MODE_DOWN

void LTEReset();      //LTE 다시 켜는 함수
void TimeFlash();     //REFRESH_INTERVAL마다 불릴 함수

/*
  데이터 전송 관련 함수 선언
*/
void CheckSession();    //세션 상태 조회하기
void SendStartData();   //접속 시작시 데이터 전송
void SendFinishData();  //접속 종료시 데이터 전송
void SendOKPP();        //사용승인 후 회신 데이터 전송(BB????OKPPFF)
void SendOKSS();        //사용해제 후 회신 데이터 전송(BB????OKSSFF)

void setup(){
  LTESetup();                     //LTE모듈 셋업
  TimerSetup();                   //Timer 셋업
  CLCDSetup();                    //CLCD 셋업
  PowerSetup();                   //전원 셋업
  Serial3.println("ATE0");        //ECHO 모두 미사용
  // Serial3.println("ATE1");        //ECHO 모드 사용

  g_ads.begin();                    //ADC 칩과 통신
  pinMode(g_pin.VIBRATE, INPUT);  //진동센서 INPUT모드로 사용

  BikeModeSetup();                //바이크 모드 셋업
  PWMSetup();                     //PWM 셋업

  g_lcd.clear();                    //LCD화면 지우기
  sprintf(g_WSOCL, "+WSOCL:%01d,CLOSE_CMPL", g_socket.SOCKET_NUMBER);//CLOSE_CMPL관련 명령어 확인부분에 넣기
  sprintf(g_DIS_IND, "+WSOEVE:%d,DIS_IND", g_socket.SOCKET_NUMBER); //DIS_IND관련 명령어 확인부분에 문자 넣어주기
  sprintf(g_WSOCO, "+WSOCO:%d,OPEN_CMPL", g_socket.SOCKET_NUMBER);  //OPEN_COMPLETE 명령어 확인부분에 문자 넣어주기
  sprintf(g_WSOCM, "+WSOCO:%d,OPEN_CM", g_socket.SOCKET_NUMBER);    //OPEN_CM 명령어 확인부분에 문자 넣어주기
}

char g_char_data[100]; //char형 배열 임시로 선언

void loop(){
  ParsingLTEData(); //읽을 데이터가 있으면서 Parsing을 안한 데이터이면 Parsing 수행
  PressButton();  //버튼을 누르면 작업 수행

  /*
    세션 확인 부분
  */
//  if(g_time.socket_check>= g_time.CHECK*2){ //확인할 시간이면 확인하기
//    CheckSocket();
//  }

  if(g_bike.is_use == g_bike.IS_USE){ //만약 사용 중인데
    if(digitalRead(g_pin.POWER) == LOW){ //꺼져있으면
      PowerOn();  //켜주기
    }
  }

  /*
    ------------------------수신관련 처리는 여기서부터------------------------
  */
  if(g_parsing_done == true){ //데이터를 Parsing하였으면 -> g_parsing_data에 데이터가 담겨 있음
    if(IS_DEVELOP){
      g_parsing_data.toCharArray(g_char_data, 100);

      Serial.println("▼▼▼▼▼▼▼▼▼▼▼▼RECEIVED-START▼▼▼▼▼▼▼▼▼▼▼▼");
      Serial.print("Received : ");
      Serial.println(g_char_data);
    }

    g_parsing_done = false; //다음 데이터를 Parsing할 수 있도록 false로 변경
    CheckReceiveCommand(); //어떤 명령어인지 확인 -> 명령어에 따라 `g_current_received`가 변경됨
    switch(g_current_received){ //받은 데이터에 따라 명령을 수행
      case RECEIVED_NOT_DEFINED:
        if(IS_DEVELOP) Serial.println("정의되지 않은 명령어");
        break;
      case RECEIVED_SUCCESS_NETWORK_CONNECTION:
        if(IS_DEVELOP) Serial.println("네트워크 접속 성공");
        CreateSocket();     //Socket 생성
        break;
      case RECEIVED_SOCKET_CHECK:
        if(g_socket.is_disconnected){ //연결이 끊어졌다는 듯
          CloseSocket();              //소켓 해제하기
        }
        if(IS_DEVELOP) Serial.println("소켓 확인 완료");
        break;
      case RECEIVED_SESSION_CLOSED:   //소켓을 끊었다는 듯
        g_socket.is_started = false;              //소켓 절차가 시작했는지
        g_socket.is_created = false;              //생성이 되었는지
        g_socket.is_connected = false;            //연결이 제대로 수립되었는지
        g_socket.is_success = false;              //서버와의 연결(BB0001CCFF)이 완료되었는지
        g_socket.is_first_connected = false;      //소켓이 제대로 연결된 후 처음인지
        g_socket.is_disconnected = false;         //소켓 연결이 헤재된 경우
        CreateSocket();     //Socket 생성
        if(IS_DEVELOP)  Serial.println("소켓 종료 후 소켓 다시 생성");
        break;
      case RECEIVED_SOCKET_CREATE_FAIL:
        if(IS_DEVELOP) Serial.println("소켓 생성 실패");
        CreateSocket(); //소켓 생성부터 다시 수행
        break;
      case RECEIVED_SOCKET_CREATE_SUCCESS:
        if(IS_DEVELOP) Serial.println("소켓 생성 완료");
        g_socket.is_started = false;  //소켓 생성은 완료하였으므로 false로 변경
        g_socket.is_created = true; //소켓이 성공적으로 생성되었음을 표시
        ConnectSocket();            //소켓 연결 시도
        break;
      case RECEIVED_SESSION_WAIT:
        if(IS_DEVELOP) Serial.println("소켓 연결을 기다리는 중");
        break;
      case RECEIVED_SESSION_COMPLETE:
        if(IS_DEVELOP) Serial.println("TCP Socket 연결 완료");
        g_socket.is_connected = true;       //정상적으로 연결되었음을 표시
        g_socket.is_first_connected = true; //Socket 연결이 제대로 수립된 후 처음임을 알림 -> 서버에 접속 시작 데이터 전송 필요
        if(IS_DEVELOP){
          Serial.print(g_time.socket_connect / g_time.TICK);
          Serial.println("초 만에 연결이 수립되었습니다");
        }
        g_time.socket_connect = 0;  //소켓 연결에 걸린 시간 초기화
        break;
      case RECEIVED_DISABLE_NETWORK:
        if(IS_DEVELOP) Serial.println("네트워크 서비스 사용 불가");
        LTEReset();         //LTE 리셋과정 수행
        break;
      case RECEIVED_DISCONNECTED_FROM_SERVER:
        if(IS_DEVELOP) Serial.println("서버에서 연결을 종료하였음");
        break;
      case RECEIVED_GPS:
        if(IS_DEVELOP) Serial.println("GPS데이터 수신");
        getGPSInformation();  //g_parsing_data부터 GPS정보로 Parsing한다
        if(IS_DEVELOP){
          if(g_gps.reliability == 'A') Serial.println("정상 GPS정보 수신");
          else if(g_gps.reliability == 'V') Serial.println("비정상 GPS정보 수신");
          Serial.println(g_gps.latitude);
          Serial.println(g_gps.longitude);
        }
        break;
      case RECEIVED_OKAA:
        if(IS_DEVELOP) Serial.println("서버로 AA데이터 송신이 제대로 완료됨");
        break;
      case RECEIVED_OKEE:
        if(IS_DEVELOP) Serial.println("서버로 EE데이터 송신이 제대로 완료됨");
        break;
      case RECEIVED_OKCC:
        if(IS_DEVELOP) Serial.println("서버로 접속 시작 데이터가 제대로 전송 됨");
        g_socket.is_ss_cc_sended = false;     //접속 시작시 데이터가 제대로 전송되었음을 확인하였으므로 false로 바꿔줌
        g_socket.is_first_connected = false;  //접속이 완료되었으므로 첫 연결이 아니라고 표시
        g_socket.is_success = true;           //서버와의 연결이 모두 마쳤음을 알림

        ConfigurateGPS();   //GPS 설정
        delay(1000);        //기다린 후
        ActivateGPS();      //GPS 정보 요청 시작
        delay(100);

        /* 이부분은 임시로 작성한 부분 -> 서버 접속 시작 하면 바로 바이크 전원 켜도록 설정 했음*/
//        if(IS_DEVELOP) Serial.println("임시로 바이크 전원을 켭니다");
//        PowerOn();                      //바이크 전원을 켠다
        /*----------------------------------*/
        break;
      case RECEIVED_OKDD:
        if(IS_DEVELOP) Serial.println("서버로 접속 해제 데이터가 제대로 전송 됨");
        break;
      case RECEIVED_PP:
        if(IS_DEVELOP) Serial.println("바이크 사용승인 요청 수신");
        PowerOn();                      //바이크 전원을 켠다
        g_socket.is_pp_received = true;   //바이크 사용승인을 받았다고 표시
        break;
      case RECEIVED_SS:
        if(IS_DEVELOP) Serial.println("바이크 사용해제 요청 수신");
        PowerOff();                         //바이크 전원을 끈다
        g_socket.is_ss_received = true;     //바이크 사용해제를 받았다고 표시
        if(IS_DEVELOP) Serial.println("is_ss_received를 true로 변경");
        break;
    }
    if(IS_DEVELOP) Serial.println("▲▲▲▲▲▲▲▲▲▲▲▲RECEIVED-END▲▲▲▲▲▲▲▲▲▲▲\n\n");
  }

  /*
    ------------------------수신관련 처리는 여기까지------------------------
  */

  if(g_socket.is_started && (g_time.socket_connect > g_time.RESPONSE*2)){
    if(IS_DEVELOP) Serial.println("시간초과로 소켓 생성 다시 요청1");
    CreateSocket();
    g_time.socket_connect = 0;
  }
  if(!g_socket.is_connected && (g_time.socket_connect > g_time.RESPONSE*2) ){  //소켓을 연결시도한 후 회신이 제시간에 안왔을 경우
    if(IS_DEVELOP) Serial.println("시간초과로 소켓 연결 다시 요청2");
    CloseSocket();
    g_time.socket_connect = 0;
  }

  /*
    XXXXXXXXXXXXXXXXXXXXXXXXXX송신관련 처리는 여기서부터XXXXXXXXXXXXXXXXXXXXXXXXXX
  */
  if(g_socket.is_connected){  //Socket이 제대로 연결이 된 상태이면 -> 서버와 통신이 가능한 상태(중간에 연결이 끊어져도 is_connected는 true임)
    CheckSendingCommand();  //어떤것을 보낼 차례인지 확인
    switch(g_current_sending){
      case SENDING_SS_CC: //접속 시작시 데이터를 전송할 차례이면
        SendStartData();                  //SS0001CCFF, 전송
        g_socket.is_ss_cc_sended = true;  //is_ss_cc_sended를 true로 해줌으로써 접속 시작시 데이터 전송을 보냈음을 알림
        g_time.current = 0; //시간 초기화
        if(IS_DEVELOP) Serial.println("처음 SS????CCFF 발송");
        break;
      case SENDING_SS_CC_RESEND: //접속 시작시 데이터의 응답을 받지못해 다시 전송할 차례
        SendStartData();
        g_time.current = 0; //시간 초기화
        if(IS_DEVELOP) Serial.println("다시 SS????CCFF 발송");
        break;
      case SENDING_AA_USE:  //사용중 정시데이터 전송할 차례
        g_time.on_time = 0; //정시시간 초기화
        GetSensorData();    //센서정보 읽은 후
        SendUseAAData();    //AA데이터 전송
        break;
      case SENDING_AA_NOT_USE:  //미사용중 정시데이터 전송할 차례
        g_time.on_time = 0;     //정시시간 초기화
        GetSensorData();        //센서정보 읽은 후
        SendNotUseAAData();     //AA데이터 전송
        break;
      case SENDING_OK_PP:       //사용승인 전송 받은 후 서버로 회신할 차례
        SendOKPP();             //서버로 BB????PPFF데이터 전송
        g_socket.is_pp_received = false;
        break;
      case SENDING_OK_SS:       //사용해제 전송 받은 후 서버로 회신할 차례
        SendOKSS();             //서버로 BB????SSFF데이터 전송
        g_socket.is_ss_received = false;
        break;
        
    }
  }
  /*
    XXXXXXXXXXXXXXXXXXXXXXXXXX송신관련 처리는 여 기 까지XXXXXXXXXXXXXXXXXXXXXXXXXX
  */
}

/*
------------------여기서부터는 loop쪽 함수 구현부분-------------------------
*/

// 소켓 생성
void CreateSocket(){
  Serial3.print("AT+WSOCR=");            //TCP Socket 생성
  Serial3.print(g_socket.SOCKET_NUMBER); //Socket 번호 지정
  Serial3.print(",");                    //구분자
  Serial3.print(g_socket.SERVER_URL);    //서버 주소 설정
  Serial3.print(",");                    //구분자
  Serial3.print(g_socket.SERVER_PORT);   //서버 포트 설정
  Serial3.print(",");                    //구분자
  Serial3.print("1");                    //프로토콜 설정, 1:TCP, 2:UDP
  Serial3.print(",");                    //구분자
  Serial3.println("0");                  //데이터 형식 지정, 0: ASCII, 1:HEX

  g_socket.is_started = true;   //Socket생성중이라고 적어놓기
  g_socket.is_created = false;  //Socket이 만들어지지 않았음을 표시
  g_socket.is_connected = false;//Socket이 연결이 되지 않았음을 표시

  g_time.socket_connect = 0;   //Socket 연결 시간 초기화
  g_time.socket_check = 0;     //Socket 체크 시간 초기화

  if(IS_DEVELOP) Serial.println("소켓 생성 요청");
}
// 소켓 연결
void ConnectSocket(){
  Serial3.print("AT+WSOCO=");              //TCP Socket 연결
  Serial3.println(g_socket.SOCKET_NUMBER); //연결할 Socket 번호

  g_time.socket_connect = 0;    //Socket 커넥트 시간 초기화

  if(IS_DEVELOP) Serial.println("소켓 연결 요청");
}
// 소켓으로 데이터 전송
void Send(const char data[], int data_length){
  Serial3.print("AT+WSOWR=");               //TCP Socket으로 데이터 전송
  Serial3.print(g_socket.SOCKET_NUMBER);    //Socket 번호 지정
  Serial3.print(",");                       //구분자
  Serial3.print(data_length);               //Socket 데이터의 길이 지정
  Serial3.print(",");                       //구분자
  Serial3.println(data);                    //데이터 전송

  if(IS_DEVELOP) Serial.println("소켓으로 데이터 전송 요청");
}
//소켓 상태 확인
//http://211.253.29.135/at/tcp/wsoss
void CheckSocket(){
  Serial3.print("AT+WSOSS=");               //TCP Socket확인
  Serial3.println(g_socket.SOCKET_NUMBER);  //Socket 번호 지정
  
  if(IS_DEVELOP) Serial.println("소켓 연결 상태 확인 요청 전송");
} 
//소켓 해제
void CloseSocket(){
  Serial3.print("AT+WSOCL=");               //TCP Socket 해제 요청
  Serial3.println(g_socket.SOCKET_NUMBER);   //Socket 번호 지정
  g_socket.is_started = false;              //소켓 절차가 시작했는지
  g_socket.is_created = false;              //생성이 되었는지
  g_socket.is_connected = false;            //연결이 제대로 수립되었는지
  g_socket.is_success = false;              //서버와의 연결(B B0001CCFF)이 완료되었는지
  g_socket.is_first_connected = false;      //소켓이 제대로 연결된 후 처음인지
  g_socket.is_disconnected = false;         //소켓 연결이 헤재된 경우

  if(IS_DEVELOP) Serial.println("소켓 해제 요청 전송");
}


// LTE의 데이터들을 Byte단위로 세어서 반환, 없을 경우 0
int AvailableLTEData(){
  return Serial3.available();
}
//LTE데이터를 END_MARKER까지 읽어들임
String ReadLTEData(){
  return Serial3.readStringUntil(END_MARKER);  //LTE Data 받기
}


//LTE데이터를 END_MARKER기준으로 파싱하여 g_parsing_data에 넣음
void ParsingLTEData(){
  if(AvailableLTEData() && g_parsing_done == false){ //읽을 수 있는 LTE데이터가 존재하며 parsing해야되면
    g_parsing_data = ReadLTEData(); //END_MARKER가 나오기 전까지 parsing
    g_parsing_done = true;
  }
}

//Parsing된 LTE데이터의 [start, end) 글자와 주어진 target문자열과 비교
bool CompareData(const char target[], int start, int end){
  if(g_parsing_data.length() < end){  //Parsing된 데이터의 길이가 짧을 경우 -> 비교 불가이므로
    return false;
  }

  String target_string = String(target); //우선 String형으로 타입캐스팅

  bool is_equal = (g_parsing_data.substring(start, end) == target_string); //substring이 target과 같은지?
  
  return is_equal;  //같으면 true, 같지않으면 false
}

//Parsing된 데이터의 명령어를 확인합니다.
//http://211.253.29.135/at/general/wind
//http://211.253.29.135/at/tcp/wsoevt


char g_check[200] = {0,};

void CheckReceiveCommand(){
  g_current_received = RECEIVED_NOT_DEFINED;  //일단 없는 명령어로 초기화
  if(CompareData("*WIND:11,INSVC", 0, 14)) g_current_received = RECEIVED_SUCCESS_NETWORK_CONNECTION;
  else if(CompareData("*WIND:12,OOS", 0, 12))   g_current_received = RECEIVED_DISABLE_NETWORK;
  else if(CompareData(g_DIS_IND, 0, 17))        g_current_received = RECEIVED_DISCONNECTED_FROM_SERVER;
  else if(CompareData("$$GPS", 0, 5))           g_current_received = RECEIVED_GPS;
  else if(CompareData("+WSORD", 0, 6)){         //서버로부터의 소켓 데이터일 경우
    g_parsing_data.toCharArray(g_char_data, 100);
    char *first = strtok(g_char_data, ":"); //+WSORD가 나올 예정
    char *second = strtok(NULL, ",");     //socket_number가 나와야 정상
    char *third = strtok(NULL, ",");      //데이터의 길이가 나와야 정상
    char *fourth = strtok(NULL, ",");     //데이터가 나와야 정상

    if(first==NULL || second==NULL || third==NULL || fourth==NULL){ //하나라도 NULL이면
      g_current_received = RECEIVED_NOT_DEFINED;  //데이터가 제대로 안 온 것
    }
    else{
      int received_socket_number = atoi(second);  //socket number가 나옴 -> 여기서는 0이 나올 예정
      int length = atoi(third);                   //Data의 길이가 나옴
      char *received_data = fourth;      //서버가 보낸 데이터가 나옴 ex) BB0001OKAAFF
      g_parsing_data = String(received_data);     //parsing데이터를 수신받은 데이터로 변경 -> Compare함수 재사용성을 위해
      if(received_socket_number == g_socket.SOCKET_NUMBER){  //서버로부터 온 데이터가 맞으면
        if(CompareData("BB",0, 2) && IsMyBike()){  //BB로 시작하며 내 Bike번호가 맞을 경우
          if(CompareData("OKAAFF", 6, 12))       g_current_received = RECEIVED_OKAA;
          else if(CompareData("OKEEFF", 6, 12))       g_current_received = RECEIVED_OKEE;
          else if(CompareData("OKCCFF", 6, 12))       g_current_received = RECEIVED_OKCC;
          else if(CompareData("OKDDFF", 6, 12))       g_current_received = RECEIVED_OKDD;
          else if(CompareData("PPFF", 6, 10))         g_current_received = RECEIVED_PP;
          else if(CompareData("SSFF", 6, 10))         g_current_received = RECEIVED_SS;
        }
      }
      else{ //서버로부터 온 데이터가 아닐 경우
        g_current_received = RECEIVED_NOT_DEFINED;
      }
    }
  }
  else if(CompareData("+WSOSS", 0, 6)){ //소켓 확인 결과일 경우
    int index = 0;

    g_parsing_data.toCharArray(g_check, 100);
    char *first = strtok(g_check, ":");
    char *second = strtok(NULL, ",");
    char *third = strtok(NULL, ",");
    char *fourth = strtok(NULL, ",");
    char *fifth = strtok(NULL, ",");
    


    int statusCode = atoi(fifth);

    
    Serial.print("status:"); Serial.println(statusCode);
    
    switch(statusCode){
      case 0:
        //TCP_CLOSED
        g_socket.is_disconnected = true;
        if(IS_DEVELOP) Serial.println("TCP_CLOSED");
        break;
      case 1:
        //TCP_DISCONNECTING
        g_socket.is_disconnected = true;
        if(IS_DEVELOP) Serial.println("TCP_DISCONNECTING");
        break;
      case 2:
        //TCP_CONNECTING
        if(IS_DEVELOP) Serial.println("TCP_CONNECTING");
        break;
      case 3: 
        //TCP_READY (Connected)
        if(IS_DEVELOP) Serial.println("TCP_READY");
        break;
      case 4: 
        //TCP_DISCONNECTING
//          g_socket.is_disconnected = true;
        if(IS_DEVELOP) Serial.println("TCP_DISCONNECTING");
        break;
      case 5: 
        //TCP_DISCONNECTED
//          g_socket.is_disconnected = true;
        if(IS_DEVELOP) Serial.println("TCP_DISCONNECTED");
        break;
      case 6: 
        //TCP_LISTEN (Server)
        if(IS_DEVELOP) Serial.println("TCP_LISTEN");
        break;
      case 7: 
        //TCP_LISTEN_CONNECTED (Child)
        if(IS_DEVELOP) Serial.println("TCP_LISTEN_CONNECTED");
        break;
      case 8: 
        //TCP_BINDED
        if(IS_DEVELOP) Serial.println("TCP_BINDED");
        break;
      default:
        if(IS_DEVELOP) Serial.println("알 수 없는 스테이터스");
        break;
    }

    g_time.socket_check = 0;  //소켓 체크를 완료하였으므로 0으로 되돌리기
    g_current_received = RECEIVED_SOCKET_CHECK;
  }
  else if(CompareData("+WSOCR:0", 0, 8))        g_current_received = RECEIVED_SOCKET_CREATE_FAIL;
  else if(CompareData("+WSOCR:1", 0, 8))        g_current_received = RECEIVED_SOCKET_CREATE_SUCCESS;
  else if(CompareData("+WSOCO:1", 0, 8))        g_current_received = RECEIVED_SESSION_WAIT;
  else if(CompareData(g_WSOCL, 0, 19)){ //소켓 해제 완료시
    g_current_received = RECEIVED_SESSION_CLOSED; //소켓 해제 완료하였음을 표시
  }
  else if(CompareData(g_WSOCO, 0, 18))          g_current_received = RECEIVED_SESSION_COMPLETE;
  else if(CompareData(g_WSOCM, 0, 16))          g_current_received = RECEIVED_SESSION_COMPLETE;
  else  g_current_received = RECEIVED_NOT_DEFINED;  //없는 명령어일 경우
}

//받은 데이터에서 내 바이크가 맞는지 확인
bool IsMyBike(){
  return CompareData(BIKE_NUMBER, 2, 6);  //데이터의 [2, 6)를 비교 -> 0001인지 비교
}

//어떤것을 보낼 차례인지 확인
void CheckSendingCommand(){
  g_current_sending = SENDING_NOT_DEFINED;  //일단 보낼것이 없음으로 바꿔준 후 -> IF문에 걸리는게 없으면 아무것도 보내지 않음
  if(g_socket.is_first_connected){                                                                                                //Socket이 연결된 후 서버와 접속 시작 데이터 보낼 차례이면
    if( (g_time.current >= g_time.RESPONSE) && (g_socket.is_ss_cc_sended)) g_current_sending = SENDING_SS_CC_RESEND;                    //접속 시작 요청을 보냈지만 응답을 제때 받지 못했으면
    else if(g_socket.is_ss_cc_sended == false) g_current_sending = SENDING_SS_CC;                                                       //접속 시작 요청을 처음으로 보낼 차례이면
  }
  else if(g_socket.is_connected){                                                                                                 //접속이 연결된 상태이면
    if(g_bike.is_emergency){                                                                                                            //긴급 데이터를 보낼 차례이면
      g_current_sending = SENDING_EE;
    }
    else if(g_socket.is_pp_received){
      g_current_sending = SENDING_OK_PP;
    }
    else if(g_socket.is_ss_received){
      g_current_sending = SENDING_OK_SS;
      if(IS_DEVELOP) Serial.println("is_ss_received");
    }
    else if((g_bike.is_use == g_bike.IS_USE) && (g_time.on_time >= g_time.INTERVAL_USE)){
      g_current_sending = SENDING_AA_USE;              //바이크가 사용중이면서 보낼시간
    }
    else if((g_bike.is_use == g_bike.IS_NOT_USE) && (g_time.on_time >= g_time.INTERVAL_NOT_USE)){
      g_current_sending = SENDING_AA_NOT_USE; //바이크가 사용중이 아니면서 보낼시간
    }
  }
}

bool IsEmergency(){
  if(true){ //긴급 데이터 보낼 차례이면 -> 코드구현 필요
    return false;
  }
  else{ //긴급 데이터를 보낼 필요 없으면
    return false;
  }
}


//GPS 기능을 활성화
void ActivateGPS(){
  Serial3.println("AT$$GPS"); //GPS 시작
  if(IS_DEVELOP)  Serial.println("GPS 시작");
}
//GPS 설정
//http://211.253.29.135/at/gps/conf
void ConfigurateGPS(){
  Serial3.print("AT$$GPSCONF=");                       //GPS셋팅
  Serial3.print("1");                                  //인터페이스, 1: UART2(AT Command)
  Serial3.print(",");                                  //구분자
  Serial3.print("0");                                  //횟수 -> GPS중단할때까지 GPS 데이터 출력
  Serial3.print(",");                                  //구분자
  Serial3.print("5000");                               //데이터 출력주기(ms) -> 5000ms마다 받아옴, (100~5000)
  Serial3.print(",");                                  //구분자
  Serial3.print("252");                                //데이터구성, 모든데이터 출력
  Serial3.print(",");                                  //구분자
  Serial3.print("1");                                  //네트워크 정보, 1: 무선 네트워크 정보 표시함
  Serial3.print(",");                                  //구분자
  Serial3.print("5");                                  //위/경도 소수점 자리수, 5: 소수점 5자리, (0~6)
  Serial3.print(",");                                  //구분자
  Serial3.print("0");                                  //SUPL, 0: SUPL 사용안함
  Serial3.print(",");                                  //구분자
  Serial3.println("0");                                //시간, 0: UTC / 1: KST

  if(IS_DEVELOP)  Serial.println("GPS 설정 완료");
}
//GPS데이터를 Parsing하여서 g_gps구조체에 정보를 저장

char g_buff[200] = {0, };
char *g_temp[20] = {0, };

char *g_gp;

bool getGPSInformation(){
  int index = 0;

  g_parsing_data.toCharArray(g_char_data, 100);
  const char delimiter[] = ",";

  g_gp = strtok(g_char_data, delimiter);  // $$GPS가 나오면 정상

  while(g_gp){ // `,`를 기준으로 GPS정보를 자름
    g_temp[index] = g_gp;
    index++;
    g_gp = strtok(NULL, delimiter);
  }
  if(index > 15){ //정보가 제대로 있으면
    g_gps.date = atoi(g_temp[1]);
    g_gps.utc = atoi(g_temp[2]);
    g_gps.latitude = atof(g_temp[3]);
    g_gps.longitude = atof(g_temp[4]);
    g_gps.altitude = atof(g_temp[5]);
    g_gps.kmPerH = atoi(g_temp[6]);
    g_gps.direct = atoi(g_temp[7]);
    g_gps.hDop = atof(g_temp[8]);
    g_gps.reliability = String(*g_temp[9]);
    // g_gps.lte = atoi(temp[9]);
    // g_gps.emm = atoi(temp[10]);
    // g_gps.esm = atoi(temp[11]);
    // g_gps.ss = atoi(temp[12]);
    // g_gps.num_sattle = atoi(temp[13]);
    // g_gps.satinfo1 = atoi(temp[14]);
    // g_gps.satinfo2 = atoi(temp[15]);
    // g_gps.satinfo3 = atoi(temp[16]);
    if(g_gps.reliability == "A"){// A일 경우 -> 정상적으로 받은 경우
      return true;
    }
  }

  return false;
}

template <typename T>
T setDigit(T value, int number_of_digit){
  long long int maxNumber = 1;
  for(int i = 0; i < number_of_digit; i++){
    maxNumber *= 10;
  }

  if(value >= maxNumber){
    value = (maxNumber-1);
  }
  if(value < 0){
    value = 0;
  }

  return value;
}


void GetSensorData(){
  int adc             = g_ads.readADC_SingleEnded(0); //출력전압, 현재 사용안함
  int raw_voltage     = g_ads.readADC_SingleEnded(1); //전압값
  int raw_temperature = g_ads.readADC_SingleEnded(2); //온도값
  int raw_current     = g_ads.readADC_SingleEnded(3); //전류값

  g_bike.voltage = (((raw_voltage - 1950) / 800.0) + 10) * 2 * 100;  //Voltage으로 환산
  g_bike.temperature = (4242 + raw_temperature) / 110.0; //Temeprature로 환산
  // g_bike.current = raw_current >= 7800 ? (raw_current - 7500) * 0.1875 * 10 : 0; //7800이상이면 사용중이므로 계산, 미만이면 0으로 -> 이건 정수부2 소수부2자리임
//  g_bike.current = raw_current >= 7800 ? (raw_current - 7500) * 0.1875 : 0; //이렇게 할경우 정수부 3, 소수부1자리임
  g_bike.soc = map(g_bike.voltage, MIN_VOLTAGE, MAX_VOLTAGE, 0, 99); //SOC 계산

  g_bike.soc = setDigit<int>(g_bike.soc, 2);
  g_bike.voltage = setDigit<int>(g_bike.voltage, 4);
  g_bike.current = setDigit<int>(raw_current, 8); //현재 그냥 센서데이터 값 그대로 넘기기로
  g_bike.temperature = setDigit<int>(g_bike.temperature, 2);

  g_gps.kmPerH = setDigit<int>(g_gps.kmPerH, 4);
  g_gps.latitude = setDigit<long int>(g_gps.latitude, 8);
  g_gps.longitude = setDigit<long int>(g_gps.longitude, 8);

  g_bike.vibrate = setDigit<int>(g_bike.vibrate, 2);

  g_lcd.setCursor(0, 1);      //처음칸으로 이동
  g_lcd.print("Volt:"); g_lcd.print(g_bike.voltage * 0.01, 1);
  g_lcd.print("(");   g_lcd.print(g_bike.soc); g_lcd.print("%)");  
  g_lcd.display();  //LCD 디스플레이에 보여줌

  if(IS_DEVELOP){
    Serial.print("전압 : "); Serial.println(g_bike.voltage);
    Serial.print("전류 : "); Serial.println(g_bike.temperature);
    Serial.print("온도 : "); Serial.println(g_bike.current);
    Serial.print("SOC : "); Serial.println(g_bike.soc);
  }
}



/*
------------------여기서부터는 setup쪽 함수 구현부분-------------------------
*/

//LTE 모듈 관련 셋업 함수
void LTESetup(){
  pinMode(g_pin.LTE, OUTPUT);           //LTE 핀 번호
  digitalWrite(g_pin.LTE, LOW);         //LTE 시작
  delay(1000);                           //딜레이
  digitalWrite(g_pin.LTE, HIGH);        //LTE 리셋 종료
  Serial.begin(9600);                   //시리얼모니터 통신채널
  Serial3.begin(115200);                //LTE 통신채널 전송속도 설정, 115200bps

  pinMode(g_pin.LTE_RESET, OUTPUT);     //LTE Reset관련 핀
  digitalWrite(g_pin.LTE_RESET, HIGH);  //LTE관련 핀 껏다가
  delay(500);
  digitalWrite(g_pin.LTE_RESET, LOW);   //다시켜기

  if(IS_DEVELOP) Serial.println("LTE Setup 완료");
}

//Timer관련 셋업 함수
void TimerSetup(){
  MsTimer2::set(g_time.REFRESH, TimeFlash);    //시간간격 및 시간마다 불릴 함수 정의
  MsTimer2::start();                           //타이머 시작

  if(IS_DEVELOP) Serial.println("Timer 셋업 완료");
}

//CLCD관련 셋업 함수
void CLCDSetup(){
  pinMode(g_pin.CLCD, OUTPUT);      //CLCD 백라이트 초기화
  digitalWrite(g_pin.CLCD, HIGH);   //CLCD 백라이트 출력
  g_lcd.begin(16, 2);               //CLCD 통신, 16문자 * 2줄 이라는 의미 인듯

  if(IS_DEVELOP) Serial.println("CLCD 셋업 완료");
}

//전원 관련 셋업 함수
void PowerSetup(){
  pinMode(g_pin.POWER, OUTPUT);     //바이크 전원
  digitalWrite(g_pin.POWER, LOW);  //바이크 전원 꺼놓기

  if(IS_DEVELOP) Serial.println("바이크 전원 셋업 완료");
}
//전원 켜기 함수
void PowerOn(){
  digitalWrite(g_pin.POWER, HIGH);  //바이크 전원 켜기;
  g_bike.is_use = g_bike.IS_USE;  //사용중이라고 기록
  
  if(IS_DEVELOP) Serial.println("바이크 전원 켜기 완료");
}
//전원 끄기 함수
void PowerOff(){
  digitalWrite(g_pin.POWER, LOW);  //바이크 전원 끄기;
  g_bike.is_use = g_bike.IS_NOT_USE;  //미사용중이라고 기록
  
  if(IS_DEVELOP) Serial.println("바이크 전원 끄기 완료");
}

//바이크 모드 관련 셋업 함수
void BikeModeSetup(){
  pinMode(g_pin.MODE_CHANGE, INPUT_PULLUP); //모드변경 스위치, 평상시 HIGH, 버튼 동작시 LOW
  pinMode(g_pin.MODE_UP, INPUT_PULLUP);     //증가 스위치
  pinMode(g_pin.MODE_DOWN, INPUT_PULLUP);   //감소 스위치

  if(IS_DEVELOP) Serial.println("바이크 모드 셋업 완료");
}

//PWM 관련 셋업 함수
void PWMSetup(){
  for(int i = 1; i < 6; i++){ //PWM스위치 1 ~ 5번까지 초기화
    pinMode(g_pin.PWM_BASE + i, OUTPUT);
  }

  //current_mode에 따라 초기 전압값 설정해주기
  for(int i = 1; i < 6; i++){ //PWM스위치 1 ~ 5번 초기 셋팅 해주기, 5개중 하나만 LOW해주기
    digitalWrite(g_pin.PWM_BASE + i, g_bike.bike_mode == i ? LOW : HIGH); //LOW일 경우 현재 바이크가 해당하는 모드라는 뜻
  }

  if(IS_DEVELOP) Serial.println("PWM 셋업 완료");
}

//Bike의 출력 전압 변경
//mode: 1~5
void ChangeBikeMode(int mode){
  g_bike.bike_mode = mode;
  for(int i = i; i < 6; i++){
    digitalWrite(g_pin.PWM_BASE + i, mode == i ? LOW : HIGH); //눌러준거에 따라 바꾸기
  }

  if(IS_DEVELOP) Serial.print("Bike Mode 변경 : "); Serial.println(mode);
}

//바이크 모드 변경 스위치에 대한 입력을 감지
bool is_pressed = false;  //눌린 상태인지 감지
bool is_done = false;     //작업이 완료되었는지 확인
int current_output_voltage = 0;
void PressButton(){
  if(g_bike.bike_mode == 1) current_output_voltage = 55;
  else if(g_bike.bike_mode == 2) current_output_voltage = 45;
  else if(g_bike.bike_mode == 3) current_output_voltage = 38;
  else if(g_bike.bike_mode == 4) current_output_voltage = 26;
  else if(g_bike.bike_mode == 5) current_output_voltage = 12;

  g_lcd.setCursor(0, 0);  //LCD 첫번째 첫줄부터
  g_lcd.print("M:");
  g_lcd.print(g_bike.bike_mode);
  g_lcd.print(" ("); g_lcd.print(current_output_voltage); g_lcd.print("V)");  //현재 전압 출력

  //출력 전압 변경부분 주석처리로 없애놓기

//  if(digitalRead(g_pin.MODE_CHANGE) == HIGH &&
//      digitalRead(g_pin.MODE_UP) == HIGH &&
//      digitalRead(g_pin.MODE_DOWN) == HIGH){  //눌린스위치 없는 경우
//      is_pressed = false; //눌린 상태가 아니라고 표시
//      is_done = false;    //작업 완료상태가 아니라고 표기
//  }
//  else{
//    is_pressed = true;  //눌린 스위치가 있다고 알림
//  }
//  if(is_pressed && is_done==false){ //스위치가 눌린 상태이면서, 작업이 완료되지 않은 경우
//    if(digitalRead(g_pin.MODE_UP) == LOW){  // 출력 전압 UP버튼인 경우
//      g_bike.bike_mode = g_bike.bike_mode !=5 ? (g_bike.bike_mode+1) : 5; //5가 아닌 경우 +1 해준다
//      ChangeBikeMode(g_bike.bike_mode); //출력 전압 변경
//      is_done = true;
//    }
//    else if(digitalRead(g_pin.MODE_DOWN) == LOW){
//      g_bike.bike_mode = g_bike.bike_mode !=1 ? (g_bike.bike_mode-1) : 1; //1이 아닌 경우 -1 해준다
//      ChangeBikeMode(g_bike.bike_mode); //출력 전압 변경
//      is_done = true;
//    }
//  }
}

//LTE 전원 리셋 함수
void LTEReset(){
  digitalWrite(g_pin.LTE, LOW);           //LTE 껐다가
  delay(1000);                            //기다렸다가
  digitalWrite(g_pin.LTE, HIGH);          //LTE 다시켜기
  delay(500);                             //기다렸다가
  digitalWrite(g_pin.LTE_RESET, HIGH);    //LTE 끄기
  delay(500);                             //기다리기
  digitalWrite(g_pin.LTE_RESET, LOW);     //LTE 다시 켜기

  if(IS_DEVELOP) Serial.println("LTE 리셋 완료");
}

//시간 새로고침 함수
void TimeFlash(){ //REFRESH에 맞게 실행 될 예정, 현재는 0.5초 주기로 카운트
  if(g_socket.is_started) g_time.socket_connect++;
  if(g_socket.is_created && g_socket.is_connected == false) g_time.socket_connect++;    //소켓 연결시간을 1씩 증가 해준다
  if(g_socket.is_connected) { //서버와 연결이 제대로 된 상태이면 
    g_time.socket_check++;  //Socket을 체크할 시간을 늘림
    g_time.current++;       //현재시간을 늘림
  }
  if(g_socket.is_success){ //모든 연결과정을 마쳤으면
    g_time.on_time++; //정시 시각을 늘려준다
  }
}

/*
  ------------------------------데이터 전송 관련 함수-------------------------
*/
void CheckSession(){
  char check_data[10+1];
  sprintf("check_data, AT+WSOSS=%01d", g_socket.SOCKET_NUMBER);
  Serial3.println(check_data);
}

void SendStartData(){
  char start_data[g_length.SS_SEND+1];
  sprintf(start_data, "SS%04dCCFF", BIKE_NUMBER_INT);
  Send(start_data, g_length.SS_SEND);

  if(IS_DEVELOP) Serial.print("접속 시작 데이터 전송 : "); Serial.println(start_data);
}
void SendFinishData(){
  char end_data[g_length.SS_SEND+1];
  sprintf(end_data, "SS%04dDDFF", BIKE_NUMBER_INT);
  Send(end_data, g_length.SS_SEND);

  if(IS_DEVELOP) Serial.print("접속 시작 데이터 전송 : "); Serial.println(end_data);
}

void SendUseAAData(){
  char AA_data[g_length.AA_SEND+1];
  sprintf(AA_data, "AA%04d%02d%04d%08d%02d%04d%08ld%08ld01%02dFF", 
//  sprintf(AA_data, "AA%04d%02d%04d%08d%02d%04d%08ld%08ld01%02d", 
    BIKE_NUMBER_INT,
    g_bike.soc,
    g_bike.voltage,
    g_bike.current,
    g_bike.temperature,
    g_gps.kmPerH,
    g_gps.reliability == "A" ? g_gps.latitude : 0, //GPS정보를 정상적으로 읽었으면 정상적인 값 주기, 아닐경우 0으로 주기
    g_gps.reliability == "A" ? g_gps.longitude : 0,
    g_bike.vibrate
    );
  Send(AA_data, g_length.AA_SEND);

  if(IS_DEVELOP) Serial.print("사용중 AA데이터 전송 : "); Serial.println(AA_data);
}

void SendNotUseAAData(){
  char AA_data[g_length.AA_SEND+1];
  sprintf(AA_data, "AA%04d%02d%04d%08d%02d%04d%08ld%08ld00%02dFF", 
//  sprintf(AA_data, "AA%04d%02d%04d%08d%02d%04d%08ld%08ld00%02d", 
    BIKE_NUMBER_INT,
    g_bike.soc,
    g_bike.voltage,
    g_bike.current,
    g_bike.temperature,
    g_gps.kmPerH,
    g_gps.reliability == "A" ? g_gps.latitude : 0, //GPS정보를 정상적으로 읽었으면 정상적인 값 주기, 아닐경우 0으로 주기
    g_gps.reliability == "A" ? g_gps.longitude : 0,
    g_bike.vibrate
    );
  Send(AA_data, g_length.AA_SEND);

  if(IS_DEVELOP) Serial.print("미사용중 AA데이터 전송 : "); Serial.println(AA_data);
}

void SendOKPP(){
  char OK_PP_data[g_length.BB_OK_SEND+1];
  sprintf(OK_PP_data, "BB%04dOKPPFF",
    BIKE_NUMBER_INT);

  Send(OK_PP_data, g_length.BB_OK_SEND);

  if(IS_DEVELOP) Serial.print("바이크 사용승인 후 회신 전송 : "); Serial.println(OK_PP_data);
}

void SendOKSS(){
  char OK_SS_data[g_length.BB_OK_SEND+1];
  sprintf(OK_SS_data, "BB%04dOKSSFF",
    BIKE_NUMBER_INT);

  Send(OK_SS_data, g_length.BB_OK_SEND);

  if(IS_DEVELOP) Serial.print("바이크 사용해제 후 회신 전송 : "); Serial.println(OK_SS_data);
}
