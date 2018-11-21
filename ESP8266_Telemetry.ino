/*
 *  ESP8266_Telemetry.ino
 *
 *  Hardware: WeMos D1 R1
 *  Wiring:
 *  D3 - Lap count sensor, switches to ground.
 *  
 *  Functional description:
 *  
 *  When the lap count sensor input goes low a UDP packet is sent to the telemetry host
 *  with the time since the lapcount has occured. This packet will be re-transmitted
 *  until the host acknowledges the packet.
 *  
 *  Changing WiFi SSID and passphrase:
 *  At any time druing startup or operation the user can send "+++" to enter
 *  Network Discovery mode. After selection of a network the user is asked to enter
 *  the network passphrase.
 *  Once connected, the SSID and passphrase are stored in the module and used 
 *  durign subsequent WiFi activities.
 *   
 *  Pins:
 *  D0 = 16   // not working
 *  D1 = 5
 *  D2 = 4
 *  D3 = 0
 *  D4 = 2 (Blue LED_BUILTIN)
 *  D5 = 14
 *  D6 = 12
 *  D7 = 13
 *  D8 = 15
 */

const byte LAP_COUNT_SENSOR_PIN = 0;

#include <ESP8266WiFi.h>
#include <WifiUdp.h>
WiFiUDP Udp;

#define UI Serial         // user interface

// Uncomment lines below to show diag info in terminal
//#define SHOWINFO_WIFIDIAG
//#define SHOWINFO_WIFICONNECTION
//#define SHOWINFO_ESP

ADC_MODE(ADC_VCC);    // switch analog input to read VCC

char wifi_ssid[32];                   // storage for WiFi SSID
char wifi_passphrase[32];;            // storage for WiFi passphrase
#define WIFI_CONNECT_TIMEOUT 15000    // max connection time for WiFi timeout

typedef union {
  long l_value;
  uint8_t bytes[4];
} LONGUNION_t;

const bool LED_ON = false;
const bool LED_OFF = true;

const long TX_INTERVAL = 5000;        // telemetry TX
long nextTX;

IPAddress t_host_ip = (127,0,0,1);              // IP of telemetry host, set by host discovery 
const char t_host_id[] = {'L', 'C', '1'};       // ID for telemetry host (used host discovery)
const int T_HOST_ID_LEN = 3;                    // length of host id
bool t_host_found = false;                      // Telemetry host has been found
const int telemetry_default_port = 2006;        // May be overridden by telemetry host discovery
unsigned int t_port = telemetry_default_port;   // Port for data exchange
const unsigned int bc_port = 2000;              // Broadcast port for telemetry host discovery
const long T_HOST_DISCOVERY_TIMEOUT = 15000;    // Timeout for telemetry host discovery

const int T_HOST_NAME_MAX_LEN = 30;
char t_host_name[T_HOST_NAME_MAX_LEN];    // storage for host name

int rxPacketSize;                       // bytes in received UDP packet
const int UDP_RX_BUFFER_SIZE = 256;
char rxPacket[UDP_RX_BUFFER_SIZE];      // buffer for incoming packets
char lcPacket[16];                      // buffer for lap count packet
char txPacket[256];                     // buffer for outgoing packets
bool t_listening = false;               // true when listening for telemetry host UDP packets
byte udp_sequence = 0;                  // sequence byte for UDP packets
const byte PACKET_TYPE_KEEP_ALIVE = 0;  // packet types 
const byte PACKET_TYPE_LAP_COUNT = 1;   // being sent 
const byte PACKET_TYPE_TELEMETRY = 2;   // to the telemetry host

volatile unsigned long lap_count_millis = 0;  // set to curent millis reading on lap counter interrupt, reset to zero when processed 
bool lap_count_signal_shadow = false;
long lap_count_signal_block_time = 10000;     // ms for lap count sensor blocking (possible multiple signals)
long lap_count_signal_block_timeout;

uint8_t macAddr[6];                       // MAC address of this device

char inputBuffer[32];                     // used for serial port input from user

volatile byte flash_byte = 0;             // used by ISR to provide pulsing bits

#define FLASH_31 0
#define FLASH_62 1
#define FLASH_125 2
#define FLASH_250 3
#define FLASH_500 4
#define FLASH_1S 5
#define FLASH_2S 6
#define FLASH_4S 7

// ********************************************************************************
//    Interrupt routines
// ********************************************************************************

/*
 * Interrupt Service Routine
 * Timer triggered interrrupt
 */
void ICACHE_RAM_ATTR onTimerISR() {
  flash_byte++;
}

/*
 * Interrupt Service Routine
 * triggered by a signal on lap counter digital input
 */
void ICACHE_RAM_ATTR onLapCountISR() {
  lap_count_millis = millis();
}

// ********************************************************************************
//    Setup and Loop functions
// ********************************************************************************

void setup() {
  UI.begin(9600);
  delay(10);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LAP_COUNT_SENSOR_PIN, INPUT_PULLUP);
  
  // We start by connecting to a WiFi network
  mylog("\n\n\nEnter +++ to activate WiFi config mode.\nConnecting to ", WiFi.SSID().c_str());
  WiFi.begin();
  
  digitalWrite(LED_BUILTIN, LED_ON);

  // get MAC address to be used as ID
  WiFi.macAddress(macAddr);
  
  // configure time interrupt
  timer1_attachInterrupt(onTimerISR);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);   // 5 ticks / us
  timer1_write( 31250 * 5); //31250 us

  // configure lap counter input interrupt
  attachInterrupt(digitalPinToInterrupt(LAP_COUNT_SENSOR_PIN), onLapCountISR, FALLING);
  
}

void loop() {
  // establish WiFi if not connected
  if (WiFi.status() != WL_CONNECTED) {
    wait_for_wifi();
  }

  // check for user input
  if (scan_user_input()) {
    wifi_select_network();
    wait_for_wifi();
  }

  // send telemetry at regular interval
  if (millis() >= nextTX) {
    nextTX += TX_INTERVAL;
    send_telemetry();
  }

  // process lapcount event
  if (lap_count_millis != 0) {
    if (!lap_count_signal_shadow) {
      mylog("Lap Count Sensor Int\n");
      lap_count_signal_shadow = true;
      lap_count_signal_block_timeout = millis() + lap_count_signal_block_time;
      send_lapcount_packet();
    } else {
      if(millis() >= lap_count_signal_block_timeout) {
        //lap_count_signal_block_timeout = 0;
        lap_count_millis = 0;
        lap_count_signal_shadow = false;
      }
    }   
  }

  // receive UDP packets from host
  if (t_listening) {
    rxPacketSize = Udp.parsePacket();
    if (rxPacketSize) {
      process_rx_packet();
    }
  }
}

/*
 * process received UDP packet
 */
void process_rx_packet() {
  mylog("Received %d bytes\n", rxPacketSize);
  Udp.read(rxPacket, rxPacketSize);
  
  // show UDP packet contents
  for (int i = 0; i<rxPacketSize; i++) {
    mylog("[%0X] ", rxPacket[i]);
  }
  UI.println();
}

// ********************************************************************************
//    Lap Count functions
// ********************************************************************************

bool send_lapcount_packet() {
  bool retVal = false;
  int i;
  LONGUNION_t elapsed_time;
  
  if (!t_host_found) return retVal;
  if (!Udp.beginPacket(t_host_ip, t_port)) {
    UI.println("Udp.beginPacket failed");
    goto send_done;
  }
  memcpy(lcPacket, macAddr, sizeof(macAddr) );
  lcPacket[6] = udp_sequence++;
  lcPacket[7] = PACKET_TYPE_LAP_COUNT;

  lap_count_millis -= 255;
  
  elapsed_time.l_value = millis() - lap_count_millis;
  for (i = 0; i < 4; i++) {
    lcPacket[8+i] = elapsed_time.bytes[i];
  }
  if (Udp.write(lcPacket, 12) != 12) {
    UI.println("Udp.write failed");
    goto send_done;
  } else {
    retVal = true;
    mylog("UDP sent: ");
    for (i=0; i<12; i++) {
      mylog("%02X,", lcPacket[i]);
    }
    mylog(" <%d> <%d> <%d>\n", millis(), lap_count_millis, elapsed_time.l_value);
  }
  Udp.endPacket();
send_done:
  return retVal;  
}

// ********************************************************************************
//    Telemetry functions
// ********************************************************************************

/*
 * send telemetry data to host
 */
void send_telemetry() {
  int txPacketSize = 0;
  if (!t_host_found) return;
  if (!Udp.beginPacket(t_host_ip, t_port)) {
    UI.println("Udp.beginPacket failed");
    goto send_done;
  }

  memcpy(txPacket, macAddr, sizeof(macAddr) );
  txPacket[6] = udp_sequence++;
  txPacket[7] = PACKET_TYPE_TELEMETRY;
  txPacketSize = 8;
  
  digitalWrite(LED_BUILTIN, LED_ON);
  if (Udp.write(txPacket, txPacketSize) != txPacketSize) {
    UI.println("Udp.write failed");
    goto send_done;
  }
  
  if (!Udp.endPacket()) {
    UI.println("Udp.endPacket failed");
  } else {
    mylog("%d: Telemerty Packet %d sent\n", millis(), udp_sequence-1);
  }
send_done:
  digitalWrite(LED_BUILTIN, LED_OFF);
}

// ********************************************************************************
//    Telemetry Host functions
// ********************************************************************************
/*
 * setup listener on telemerty port for replys from telemetry host.
 * returns true on success
 */
bool setup_t_port_listening () {
  if (Udp.begin(t_port) != 1) {
    mylog("!! ERROR: unable to listen on UPD port %d\n",t_port);
    return false;
  }
  t_listening = true;
  mylog("Listening on local UPD port %d\n",t_port);
  return true;
}

/*
 * Wait for broadcast from telemetry host
 * timeout: timeout in ms
 * returns: true on success and false on fail
 */
bool discover_telemetry_host(long timeout) {
  // exit if WiFi is not connected
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  // Start listening on broadcast port
  if (Udp.begin(bc_port) != 1) {
    return false;
  }
  
  
  long timeout_value = millis() + timeout;
  int packetSize;
  int bytesRead = 0;

  // wait for broadcast packet from telemetry host
  while (millis() < timeout_value) {
    packetSize = Udp.parsePacket();
    if(packetSize) {
       // read the packet into packetBufffer
      bytesRead = Udp.read(rxPacket,UDP_RX_BUFFER_SIZE);    
      if (validateTelemetryHost(bytesRead)) {
        Udp.flush();
        Udp.stop();
        setup_t_port_listening();
        return true;
      }
    }
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_1S));
    // Check for user input whci indicates the user wants to change WiFi network
    if (UI.available()) return false;
  }
  return false;
}

/*
 * Validate UDP packet from telemetry broadcast t
 * returns: true if host is valid, otherwise false
 */
bool validateTelemetryHost(int bufsize) {
  // ID sufficient length?
  if (bufsize < T_HOST_ID_LEN) {
    return false;
  }
  // check ID contents
  for (int i=0; i<T_HOST_ID_LEN; i++) {
    if (rxPacket[i] != t_host_id[i]) return false;
  }
  // We have a valid ID, record the host IP
  t_host_ip = Udp.remoteIP();
  t_host_found = true;
  mylog("Telemetry host: ");
  UI.println(t_host_ip);

  // check packet for more host information
  rxPacket[bufsize] = 0;  // force end of string
  char *token = strtok(rxPacket, "\t");
  int tokencount = 0;
  unsigned int port;
  while (token != 0) {
    tokencount++;
    switch(tokencount) {
      case 1:
        break;
      case 2:
        port = atoi(token);
        if (port > 0 && port <= 65535 ) {
          t_port = port;
        } else {
          t_port = telemetry_default_port;
        }
        mylog("Telemetry port: %d\n",  t_port);
        break;
      case 3:
        strncpy(t_host_name, token, T_HOST_NAME_MAX_LEN);
        mylog("Telemetry host: %s\n", t_host_name );
        break;
      default:
        // unknown token
        break;
    }
    token = strtok(0, "\t");
  }
  
  return true;
}


// ********************************************************************************
//    WiFi functions
// ********************************************************************************

/*
 * Allow user to change WiFi SSID and password
 */
void wifi_select_network() { 
  byte numSsid;
  int thisNet;
  
startAgain:
  UI.println("\n** Scanning Nearby Networks **");
  // scan for nearby networks:
  numSsid = WiFi.scanNetworks();
  // print the list of networks seen:
  mylog("SSID List: [%d]\n", numSsid);
  // print the network number and name for each network found:
  for (thisNet = 0; thisNet<numSsid; thisNet++) {
    mylog("%d) [%ddBm] %s\n", thisNet, WiFi.RSSI(thisNet), WiFi.SSID(thisNet).c_str() );
  }
  mylog("Select Network [0-%d] and press Enter: ", numSsid-1);
  if (read_line() <= 0) {
    mylog("Error - Nothing selected\n");
    goto startAgain;
  }
  mylog("%s\n", inputBuffer);
  thisNet = atoi(inputBuffer);
  if ( (thisNet >= numSsid)  || (thisNet < 0)) {
    mylog("Error - Invalid selection\n");
    goto startAgain;
  }
  mylog("Please enter pass phrase for %s : ", WiFi.SSID(thisNet).c_str() );
  if (read_line() < 0) {
    mylog("Error - A valid passphrase must be entered\n");
    goto startAgain;
  }
  mylog("**passphrase**\n\nConnecting to %s\n", WiFi.SSID(thisNet).c_str());
  // save ssid and passphrase
  strcpy(wifi_ssid, WiFi.SSID(thisNet).c_str());
  strcpy(wifi_passphrase, inputBuffer);
  
  // connect usign new credentials
  WiFi.disconnect(true);                  // this will clear the previous credentials
  WiFi.begin(wifi_ssid, wifi_passphrase);
}

/*
 * Wait for WiFi to connect
 * If not connected within timeout period the user will be prompted to select a new WiFi network
 * Once the WiFi is conneced we wait for a broadcast packet from the telemetry host 
 */
void wait_for_wifi() {
  long wifi_timeout;
start_again:  
  wifi_timeout = millis() + WIFI_CONNECT_TIMEOUT;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);             // do not remove, no delay will crash the ESP8266
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_250));
    if (millis() >= wifi_timeout) {
      mylog("\nWiFi timeout trying to connect to %s\n", WiFi.SSID().c_str());
      wifi_select_network();
      wifi_timeout = millis() + WIFI_CONNECT_TIMEOUT;
    }
    if (scan_user_input()) {
      wifi_select_network();
      wifi_timeout = millis() + WIFI_CONNECT_TIMEOUT;
    }
  }
  //mylog("WiFi Connected, [%02X:%02X:%02X:%02X:%02X:%02X]\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);

#ifdef SHOWINFO_WIFIDIAG
  UI.println("");
  WiFi.printDiag(Serial);
#endif

  show_wifi_info();
  esp_info();

  UI.print("Waiting for telemetry host broadcast ");
  while (!t_host_found ) {   
    UI.print(".");
    if (discover_telemetry_host(T_HOST_DISCOVERY_TIMEOUT)) {
      UI.println(" ");
    }
    // allow user to interrupt and select different network
    if (scan_user_input()) {
      UI.println(" ");
      wifi_select_network();
      goto start_again;
    }   
  }
  nextTX = millis() + TX_INTERVAL;
}


// ********************************************************************************
//    Utility functions
// ********************************************************************************

/*
 * Scan for use input of +++ to enter WiFi config mode
 * returns true if the user has entered +++ otherwise false
 */
bool scan_user_input() {
  if (UI.available()) {
    if (read_line() == 3) {
      for (int i=0; i<3; i++) {
        if (inputBuffer[i] != '+') return false;
      }
      // drain the input buffer
      while (UI.available()) {
        UI.read();
      }
      return true;
    }
  }
  return false;
}

/*
 * read one line of user input into buffer 
 * returns the number of characters read into the input buffer
 */
int read_line() {
  int cnt = 0;
  char c;
  while (1) {
    if (UI.available()) {
      c = UI.read();
      if (c >= 0x1F) {
        inputBuffer[cnt++] = c;
      }
      if ((c == '\n') || (cnt == sizeof(inputBuffer)-1)) {
        inputBuffer[cnt] = '\0';
        return cnt;    
      }
    }
  }
}


/* 
 *  print debug output on console interface
 */
void mylog(const char *sFmt, ...)
{
  char acTmp[128];       // place holder for sprintf output
  va_list args;          // args variable to hold the list of parameters
  va_start(args, sFmt);  // mandatory call to initilase args 

  vsprintf(acTmp, sFmt, args);
  UI.print(acTmp);
  // mandatory tidy up
  va_end(args);
  return;
}

void show_wifi_info() {
  if (WiFi.status() == WL_CONNECTED) {
    mylog("\nWiFi status: Connected\n");
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    mylog("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    IPAddress ip = WiFi.localIP();
    mylog("IP address: %s\n", ip.toString().c_str() );
    ip = WiFi.subnetMask();
    mylog("Subnet mask: %s\n", ip.toString().c_str() );
    mylog("Hostname: %s\n", WiFi.hostname().c_str() );
    mylog("RSSI: %d dBm\n", WiFi.RSSI() );
  } else {
    mylog("\nWiFi status: Not Connected\n");
  }

}

void esp_info() {
#ifdef SHOWINFO_ESP
  mylog("\nChip info:\n");
  mylog("Reset reason: %s\n", ESP.getResetReason().c_str() );
  mylog("Chip ID: %u\n", ESP.getChipId() );
  mylog("Core Version: %s\n", ESP.getCoreVersion().c_str() );
  mylog("SDK Version: %s\n", ESP.getSdkVersion() );
  mylog("CPU Frequency: %uMHz\n", ESP.getCpuFreqMHz() );
  mylog("Sketch size: %u\n", ESP.getSketchSize() );
  mylog("Free Sketch space: %u\n", ESP.getFreeSketchSpace() );
  mylog("Flash Chip ID: %u\n", ESP.getFlashChipId() );
  mylog("Flash Chip size: %u (as seen by SDK)\n", ESP.getFlashChipSize() );
  mylog("Flash Chip size: %u (physical)\n", ESP.getFlashChipRealSize() );
  mylog("Flash Chip speed: %uHz\n", ESP.getFlashChipSpeed() );
  mylog("VCC: %.2fV\n", (float)ESP.getVcc() / 896 );
#endif
}


