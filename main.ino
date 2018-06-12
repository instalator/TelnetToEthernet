/*
 * This sketch allows you to make a configurable
 * Serial-To-Ethernet gate, so you could just plug
 * your arduino to the network and to the serial device
 * and it will transfer all the data from serial port
 * to the network and vice versa.
 */
#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp2.h>

// Port to send commands
#define CMD_PORT 23
// Port for configuration
#define CONTROL_PORT 24
// UDP port
#define UDP_PORT 8788
// label buffer size
#define LBLSIZE 64

// replace this with your mac address
byte mac[] = {
  0x93, 0xA6, 0xD2, 0x50, 0x5C, 0x4E
};
/*
  For manual configuration of the network uncomment the following lines
  and change the Ethernet.begin arguments in the setup() function
*/
IPAddress ip(192, 168, 1, 53);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

/*
 * ==================================
 * Everything below should just work
 * ==================================
 */

// structure to store settings of the serial port
struct ComSettings{
  char label[LBLSIZE];
  long baudrate;
  char parity;
  long wordlength;
  long stopbits;
};

ComSettings settings;

// default serial port configuration
ComSettings defaults = {"Undefined", 9600, 'N', 8, 1};
byte serialSettings(struct ComSettings s){
  // this function returns serial configuration for Serial library
  long conf = 0;
  long wl = 3;
  if(s.wordlength >= 5 && s.wordlength <= 8){
    wl = s.wordlength-5;
  }
  long stp = 0;
  if(s.stopbits==1 || s.stopbits==2){
    stp = s.stopbits-1;
  }
  long p = 0;
  if(s.parity=='E'){
    p=2;
  }
  if(s.parity=='O'){
    p=3;
  }
  conf = (p << 4) | (stp << 3) | (wl << 1);
  return conf;
}

bool alreadyConnected = false;
bool controlAlreadyConnected = false;

EthernetServer cmdServer(CMD_PORT);
EthernetServer controlServer(CONTROL_PORT);
EthernetUDP Udp;

String cmd = "";
//
void parseCmd(String s, EthernetClient client){
  if(s=="help"){
    client.println("Available commands:");
    client.println("? - get <label>,<baudrate>,<parity>,<wordlength>,<stopbits>");
    client.println("label [string] - get or set custom label for this box (up to 32 characters)");
    client.print("baudrate [value] - get or set baudrate ");
    client.println("(300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 115200)");
    client.println("parity [value] - get or set parity (N, E, O)");
    client.println("wordlength [value] - get or set wordlength (5, 6, 7, 8)");
    client.println("stopbits [value] - get or set stopbits (1, 2)");
    client.println("save - saves current settings to EEPROM memory");
    client.println("load - loads settings from EEPROM memory");
  }

  bool changed = false;

  if(s=="save"){
    EEPROM.put(0, settings);
    client.println("Saved!");
  }

  if(s=="load"){
    EEPROM.get(0, settings);
    client.println("Loaded!");
    changed = true;
  }

  if(s=="?"){
    client.print(settings.label);
    client.print(",");
    client.print(settings.baudrate);
    client.print(",");
    client.print(settings.parity);
    client.print(",");
    client.print(settings.wordlength);
    client.print(",");
    client.println(settings.stopbits);
  }

  int l = s.length();
  if(s.startsWith("label")){
    if(l>6){
      s.substring(6).toCharArray(settings.label, LBLSIZE);
      changed = true;
    }
    client.println(settings.label);
  }
  if(s.startsWith("baudrate")){
    if(l>9){
      settings.baudrate=s.substring(9).toInt();
      changed = true;
    }
    client.println(settings.baudrate);
  }
  if(s.startsWith("parity")){
    if(l>7){
      settings.parity=s.charAt(7);
      changed = true;
    }
    client.println(settings.parity);
  }
  if(s.startsWith("wordlength")){
    if(l>11){
      settings.wordlength=s.substring(11).toInt();
      changed = true;
    }
    client.println(settings.wordlength);
  }
  if(s.startsWith("stopbits")){
    if(l>9){
      settings.stopbits=s.substring(9).toInt();
      changed = true;
    }
    client.println(settings.stopbits);
  }
  if(changed){
    reopenSerial();
  }
}

void reopenSerial(){
  Serial.begin(settings.baudrate, SERIAL_8N1);
  controlServer.println("Settings changed:");
  controlServer.print(settings.label);
  controlServer.print(",");
  controlServer.print(settings.baudrate);
  controlServer.print(",");
  controlServer.print(settings.parity);
  controlServer.print(",");
  controlServer.print(settings.wordlength);
  controlServer.print(",");
  controlServer.println(settings.stopbits);
}

void checkControl(){
  EthernetClient client = controlServer.available();

  if (client) {
    if (!controlAlreadyConnected) {
      // clean out the input buffer:
      client.flush();
      controlAlreadyConnected = true;
    }

    if (client.available() > 0){
      char c = client.read();
      if(c=='\n'){
        parseCmd(cmd, client);
        cmd = "";
      }else{
        if(c!='\r'){ // ignoring \r
          cmd += c;
        }
      }
    }
  }

}

void printConfig(){
    Serial.print("IP-address: ");
    Serial.println(Ethernet.localIP());
    if(!Serial){
      Serial.println("Serial port is closed");
    }else{
      Serial.println("Serial port is opened");
    }
    Serial.println("Serial configuration:");
    Serial.print("Label: ");
    Serial.println(settings.label);
    Serial.print("Baudrate: ");
    Serial.println(settings.baudrate);
    Serial.print("Parity: ");
    Serial.println(settings.parity);
    Serial.print("Wordlength: ");
    Serial.println(settings.wordlength);
    Serial.print("Stopbits: ");
    Serial.println(settings.stopbits);
}

// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,
char  ReplyBuffer[] = "Serial gate v1.1";       // a string to send back

void checkUDP(){
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize){
    // read the packet into packetBufffer
    Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[packetSize] = '\0';

    if(strcmp(packetBuffer,"?") == 0){
      // send a reply, to the IP address and port that sent us the packet we received
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      Udp.write(ReplyBuffer);
      Udp.endPacket();
    }
  }
}

void setup() {
  // For DHCP use only mac. For manual configuration use all parameters.

//   Ethernet.begin(mac);
  Ethernet.begin(mac, ip, gateway, subnet);

  EEPROM.get(0, settings);
  // very stupid check
  if(settings.baudrate<300){
    EEPROM.put(0, defaults);
    EEPROM.get(0, settings);
  }

  cmdServer.begin();
  controlServer.begin();
  Udp.begin(UDP_PORT);
  Serial.begin(settings.baudrate, SERIAL_8N1);
}

void loop() {
  // wait for a new client:
  EthernetClient client = cmdServer.available();

  // to disable telnet control just comment the line below
  checkControl();
  checkUDP();

  char c;

  // transfer all bytes from client to Serial
  if (client) {
    if (!alreadyConnected) {
      // clean out the input buffer:
      client.flush();
      alreadyConnected = true;
    }

    if (client.available() > 0){
      c = client.read();
      Serial.write(c);
    }
  }

  // transfer all bytes to the cmdServer
  // (all connected clients will recieve it)
  if(Serial.available() > 0){
    c = Serial.read();
    cmdServer.write(c);
  }
}
