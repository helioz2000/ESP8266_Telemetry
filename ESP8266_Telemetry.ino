/*
 *  ESP8266_Telemetry.ino
 *
 *  Hardware: WeMos D1 R1
 *  Wiring:
 *   
 *   
 */

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

const long FLASH_INTERVAL = 2000;     // flash when connected to WiFi
const long FLASH_DURATION = 30;

const long TX_INTERVAL = 5000;        // telemetry TX

long nextflash, flashoff;
long nextTX;

WiFiUDP Udp;
IPAddress t_host_ip = (127,0,0,1);
const char t_host_id[] = {'L', 'C', '1'};
const int T_HOST_ID_LEN = 3;
bool t_host_found = false;
const unsigned int t_port = 2006;     // Port for data exchange
const unsigned int bc_port = 2000;    // Broadcast port for telemetry host discovery
const long T_HOST_DISCOVERY_TIMEOUT = 30000;    // Timeout for telemetry host discovery


const int UDP_RX_BUFFER_SIZE = 256;
char rxPacket[UDP_RX_BUFFER_SIZE];                   // buffer for incoming packets
char txPacket[256];                   // buffer for outgoing packets
bool bc_listening = false;            // true when listening for broadcast for host discovery

byte flash_byte = 0;
//bool flash_mem;

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
void ICACHE_RAM_ATTR onTimerISR(){
  //if (flash_byte <= 255)
    flash_byte++;
  //else
  //  flash_byte = 1;
}

void setup() {
  Serial.begin(9600);
  delay(10);

  pinMode(LED_BUILTIN, OUTPUT);
  
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

  Serial.print("Discovering telemetry host ");
  while (!t_host_found ) {   
    Serial.print(".");
    discover_telemetry_host(T_HOST_DISCOVERY_TIMEOUT);   
  }
  nextflash = millis() + FLASH_INTERVAL;
  nextTX = millis() + TX_INTERVAL;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wait_for_wifi();
  } else {
    /*
    if (millis() >= nextflash) {
      nextflash += FLASH_INTERVAL;
      digitalWrite(LED_BUILTIN, LED_ON);
      flashoff = millis() + FLASH_DURATION;
    } else {
      if (millis() >= flashoff)
        digitalWrite(LED_BUILTIN, LED_OFF);
    }
    */
    if (millis() >= nextTX) {
      nextTX += TX_INTERVAL;
      send_telemetry();
    }
  }
}

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
      mylog("UDP receive %d %d\n", packetSize, bytesRead );
      if (validateTelemetryHost(bytesRead)) {
        return true;
      }
    }
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_1S));
  }
  return false;
}

/*
 * Validate UDP packet to telemtry host
 * returns: true if host is valid, otherwise false
 */
bool validateTelemetryHost(int bufsize) {
  // ID sufficinet length?
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
  Serial.print("Found Telemetry host: ");
  Serial.println(t_host_ip);
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


