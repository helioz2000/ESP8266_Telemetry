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

// Uncomment lines below to show diag info in terminal
//#define SHOWINFO_WIFIDIAG
//#define SHOWINFO_WIFICONNECTION
//#define SHOWINFO_ESP

ADC_MODE(ADC_VCC);    // switch analog input to read VCC

// Fill in network details below:
const char* ssid     = "";
const char* password = "";

const bool LED_ON = false;
const bool LED_OFF = true;

const long TX_INTERVAL = 5000;        // telemetry TX
long nextTX;

WiFiUDP Udp;
IPAddress t_host_ip = (127,0,0,1);
const char t_host_id[] = {'L', 'C', '1'};
const int T_HOST_ID_LEN = 3;
bool t_host_found = false;
const int telemetry_default_port = 2006;
unsigned int t_port = telemetry_default_port;     // Port for data exchange
const unsigned int bc_port = 2000;    // Broadcast port for telemetry host discovery
const long T_HOST_DISCOVERY_TIMEOUT = 30000;    // Timeout for telemetry host discovery

const int T_HOST_NAME_MAX_LEN = 30;
char t_host_name[T_HOST_NAME_MAX_LEN];    // storage for host name

const int UDP_RX_BUFFER_SIZE = 256;
char rxPacket[UDP_RX_BUFFER_SIZE];                   // buffer for incoming packets
char txPacket[256];                   // buffer for outgoing packets
bool bc_listening = false;            // true when listening for broadcast for host discovery

volatile bool lap_count_trigger = false;
bool lap_count_signal_shadow = false;
long lap_count_signal_block_time = 10000;    // ms for lap count sensor blocking (possible multiple signals)
long lap_count_signal_block_timeout;

volatile byte flash_byte = 0;

#define FLASH_31 0
#define FLASH_62 1
#define FLASH_125 2
#define FLASH_250 3
#define FLASH_500 4
#define FLASH_1S 5
#define FLASH_2S 6
#define FLASH_4S 7

/*
 * Interrupt Service Routine
 * Timer triggered interrrupt
 */
void ICACHE_RAM_ATTR onTimerISR() {
  flash_byte++;
}

void ICACHE_RAM_ATTR onLapCountISR() {
  lap_count_trigger = true;
}

void setup() {
  Serial.begin(9600);
  delay(10);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LAP_COUNT_SENSOR_PIN, INPUT_PULLUP);
  
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid); 
  WiFi.begin(ssid, password);
  digitalWrite(LED_BUILTIN, LED_ON);
  
  // configure time interrupt
  timer1_attachInterrupt(onTimerISR);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);   // 5 ticks / us
  timer1_write( 31250 * 5); //31250 us

  // configure lap counter input interrupt
  attachInterrupt(digitalPinToInterrupt(LAP_COUNT_SENSOR_PIN), onLapCountISR, FALLING);
  
}

void wait_for_wifi() {
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);             // do not remove, no delay will crash the ESP8266
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_250));
    //Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");  

#ifdef SHOWINFO_WIFIDIAG
  Serial.println("");
  WiFi.printDiag(Serial);
#endif

  wifi_info();
  esp_info();

  Serial.print("Waiting for telemetry host broadcast ");
  while (!t_host_found ) {   
    Serial.print(".");
    discover_telemetry_host(T_HOST_DISCOVERY_TIMEOUT);   
  }
  nextTX = millis() + TX_INTERVAL;
}

void loop() {
  // establish WiFi if not connected
  if (WiFi.status() != WL_CONNECTED) {
    wait_for_wifi();
  }

  // send telemetry at regular interval
  if (millis() >= nextTX) {
    nextTX += TX_INTERVAL;
    send_telemetry();
  }

  if (lap_count_trigger) {
    if (!lap_count_signal_shadow) {
      mylog("Lap Count Sensor Int\n");
      lap_count_signal_shadow = true;
      lap_count_signal_block_timeout = millis() + lap_count_signal_block_time;
    } else {
      if(millis() >= lap_count_signal_block_timeout) {
        //lap_count_signal_block_timeout = 0;
        lap_count_trigger = false;
        lap_count_signal_shadow = false;
      }
    }   
  }
}
/*
 * send telemetry data to host
 */
void send_telemetry() {
  if (!t_host_found) return;
  if (!Udp.beginPacket(t_host_ip, t_port)) {
    Serial.println("Udp.beginPacket failed");
    goto send_done;
  }
  
  txPacket[0] = 0x31;
  txPacket[1] = 0x32;
  txPacket[2] = 0x33;
  txPacket[3] = 0x34;

  digitalWrite(LED_BUILTIN, LED_ON);
  if (Udp.write(txPacket, 4) != 4) {
    Serial.println("Udp.write failed");
    goto send_done;
  }
  
  if (!Udp.endPacket()) {
    Serial.println("Udp.endPacket failed");
  } else {
    Serial.print(millis());
    Serial.println(": Udp Packet sent");
  }
send_done:
  digitalWrite(LED_BUILTIN, LED_OFF);
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
  bc_listening = true;
  
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
        return true;
      }
    }
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_1S));
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
  Serial.println(t_host_ip);

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


// print debug output on console interface
void mylog(const char *sFmt, ...)
{
  char acTmp[128];       // place holder for sprintf output
  va_list args;          // args variable to hold the list of parameters
  va_start(args, sFmt);  // mandatory call to initilase args 

  vsprintf(acTmp, sFmt, args);
  Serial.print(acTmp);
  // mandatory tidy up
  va_end(args);
  return;
}

void wifi_info() {
#ifdef SHOWINFO_WIFICONNECTION
  if (WiFi.status() == WL_CONNECTED) {
    mylog("\nWiFi status: Connected\n");
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    mylog("Connected, mac address: %02x:%02x:%02x:%02x:%02x:%02x\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    IPAddress ip = WiFi.localIP();
    mylog("IP address: %s\n", ip.toString().c_str() );
    ip = WiFi.subnetMask();
    mylog("Subnet mask: %s\n", ip.toString().c_str() );
    mylog("Hostname: %s\n", WiFi.hostname().c_str() );
    mylog("RSSI: %d dBm\n", WiFi.RSSI() );
  } else {
    mylog("\nWiFi status: Not Connected\n");
  }
#endif
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


