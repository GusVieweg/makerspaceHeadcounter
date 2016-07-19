/*****************************************************************
Much of this code is largely based on David Mellis' WebClient
example in the Ethernet library and Jim Lindblom's phant server system.
*****************************************************************/

#include <SPI.h> // Required to use Ethernet
#include <Ethernet.h> // The Ethernet library includes the client
#include <EthernetUdp.h> // Library that retrieves what time it is.

///////////////////////
// Ethernet Settings //
///////////////////////

byte mac[] = { MAC_ADDRESS };
unsigned int localPort = 8888;
IPAddress timeServer(132, 163, 4, 101); // time-a.timefreq.bldrdoc.gov NTP server

// if you don't want to use DNS (and reduce your sketch size)
// use the numeric IP instead of the name for the server:
char server[] = "data.sparkfun.com";    // name address for data.sparkFun (using DNS)

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(STATIC_IP);
const int NTP_PACKET_SIZE= 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets 

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

// Initialize the Ethernet client library
// with the IP address and port of the server 
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;

/////////////////
// Phant Stuff //
/////////////////

const String publicKey = "PUBLIC_KEY";
const String privateKey = "PRIVATE_KEY";
const byte NUM_FIELDS = 2;
const String fieldNames[NUM_FIELDS] = {"hour","visitors"};
String fieldData[NUM_FIELDS];

//////////////////////
// Input Pins, Misc //
//////////////////////

int persons = 0 ;                   // People who enter.
int pirPin = 3 ;                    // Motion detector pin.
int yellowLight = 4 ;               // FLASHES if device is working properly.
int greenLight = 7 ; 				// ON if data is sending.
int redLight = 8 ;   				// ON if data isn't sending.
unsigned long int bigSeconds = 0 ;  // Number of seconds since January 1, 1900.
boolean recentlySent = false ;      // Variable to avoid duplicating results.

// Initialize serial monitor, pin modes, and Ethernet setup.
void setup()
{
  Serial.begin(9600);
  Serial.println ( "Serial on!" ) ;
  pinMode ( pirPin , INPUT ) ;
  pinMode ( yellowLight , OUTPUT ) ;
  pinMode ( greenLight , OUTPUT ) ;
  pinMode ( redLight , OUTPUT ) ;
  
  // Set Up Ethernet:
  setupEthernet();
  Udp.begin(localPort);
}

int whatDayIsIt ( unsigned long long seconds )
{
    unsigned long int leftoverSecs ;
    int todayIs = 0 ;
    leftoverSecs = seconds % 604800 ;
    todayIs = leftoverSecs / 86400 ;
    return todayIs ; 				// if 0, it's Monday, if 1, it's Tuesday, etc.
}

// Function to convert seconds to current hour.
int whatistheHour ( unsigned long int x ) {
  int hourIs = 0 ;
  hourIs = ( ( x % 86400 ) / 3600 ) - 4 ;
  if ( hourIs < 0 ) {
    return ( hourIs + 24 ) ;
  }
  else {
    return hourIs ;
  }
}

// Function to convert standard time to military time.
int militaryTime ( int hourglass , char h ) {
  int currentDay = whatDayIsIt ( bigSeconds ) ;
  int revisedTime = 0 ;
  if ( h == 'A' ) {
    revisedTime = hourglass ;
  }
  else if ( ( currentDay != 4 ) && ( h == 'P' ) ) {
    revisedTime = hourglass + 12 ;
  }
  else if ( ( currentDay == 4 ) && ( h == 'P' ) ) {
    return 17 ; // If today is Friday, the closing hour is 5:00P.
  } 
  return revisedTime ;
}

// Function that checks the time, date, Makerspace hours, and sends data accordingly.
void checkData() {
  boolean makerspaceOpen = true ;
  int openingHour = militaryTime ( 10 , 'A' ) ;
  int closingHour = militaryTime ( 10 , 'P' ) ;
  int hourNow = 0 ;
  sendNTPpacket (timeServer); // send an NTP packet to a time server
    // wait to see if a reply is available
  delay(1000);
  if ( Udp.parsePacket() ) {  
    // We've received a packet, read the data from it
    Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  
    
	// combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    bigSeconds = secsSince1900 ;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);
  }
  
  hourNow = whatistheHour ( bigSeconds ) ;

  Serial.print ( "The hour now is " ) ;
  Serial.println (hourNow - 1) ;
  Serial.print ( "The opening hour is " ) ;
  Serial.println (openingHour) ;
  Serial.print ( "The closing hour is " ) ;
  Serial.println (closingHour) ;
  Serial.print( "We have had ");
  Serial.print( (persons > 0) ? (persons / 2 + 1) : 0);
  Serial.println(" visitors this hour");
  
  if ( ( hourNow >= openingHour ) && ( hourNow <= closingHour ) ) {
    makerspaceOpen = true ;
    Serial.println ( "The Makerspace is open!" ) ;
  }
  else {
    makerspaceOpen = false ;
    Serial.println ( "The Makerspace is not open." ) ;
  }
  if ( ( ( bigSeconds % 3600 ) > 0 ) && ( ( bigSeconds % 3600 ) < 23 ) && ( recentlySent != true )) {
      Serial.println("Posting!");
      if(persons > 0) persons = ( persons / 2 ) + 1 ;       // Experimental and simplistic algorithm.
      fieldData[0] = String ( hourNow - 1 ) ;  				// Since time is in UDP, this posts the hour in EST.
      fieldData[1] = String ( persons ) ;  					// Number of people who have walked in.
      postData(); 											// the postData() function does all the work, 
															// check it out below.
      postDataToGoogle(hourNow, persons);
      
      // If we just posted for 10:00PM or 10:00 AM, reboot
      if((hourNow - 1) == 10 || (hourNow - 1 == 22))
      {
        Serial.println("It's 10:00. Rebooting!");
        system("reboot");
      }
      
      recentlySent = true ;
      
      persons = 0 ;
    }
}

// Each time the motion sensor is tripped, increment persons entered.
void counterFunction() {
  persons ++ ;
  Serial.println( persons ) ;
  delay ( 2000 ) ;
}

int countDown = 0 ;  // Variable used as an arbitrary counter.

// The "brains" of the program.  Makes motion detector run and checks the data/time every few seconds.
void loop()
{
  int pirVal = digitalRead(pirPin) ;
  digitalWrite ( yellowLight , HIGH ) ;
  if ( pirVal == 0 ) {
    counterFunction() ;
  }
  if ( ( countDown == 20 ) && ( recentlySent == false ) ) {
    digitalWrite ( yellowLight , LOW ) ;
    checkData() ;
    delay ( 500 ) ;
    countDown = 0 ;
  }
  else if ( ( countDown == 20 ) && ( recentlySent == true ) ) {
    recentlySent = false ;
    countDown = 0 ;
  }
  delay ( 500 ) ;
  countDown ++ ;
  //Serial.print ( countDown ) ;
}

// Snagged from EthernetUDP example page.
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE); 
  
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49; 
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:         
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer,NTP_PACKET_SIZE);
  Udp.endPacket(); 
}

// Snagged from Sparkfun's Phant page.
void postData()
{ 
  // Make a TCP connection to remote host
  if (client.connect(server, 80))
  {
    // Post the data! Request should look a little something like:
    // GET /input/publicKey?private_key=privateKey&light=1024&switch=0&name=Jim HTTP/1.1\n
    // Host: data.sparkfun.com\n
    // Connection: close\n
    // \n
    client.print("GET /input/");
    client.print(publicKey);
    client.print("?private_key=");
    client.print(privateKey);
    for (int i=0; i<NUM_FIELDS; i++)
    {
      client.print("&");
      client.print(fieldNames[i]);
      client.print("=");
      client.print(fieldData[i]);
    }
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(server);
    client.println("Connection: close");
    client.println();
    digitalWrite ( greenLight, HIGH ) ;
    digitalWrite ( redLight, LOW ) ;
  }
  else
  {
    Serial.println(F("Connection failed"));
    digitalWrite ( redLight, HIGH ) ;
    digitalWrite ( greenLight, LOW ) ;
  } 

  // Check for a response from the server, and route it
  // out the serial port.
  while (client.connected())
  {
    if ( client.available() )
    {
      client.stop() ;
    }      
  }
  Serial.println();
  Serial.println("Stopping client and restarting!") ;
}

void postDataToGoogle(int hour, int visitors) {
  String pythonCall = "python /home/root/datalogger.py ";
  String pythonArguments = String(hour) + " " + String(visitors);
  String systemCall = String(pythonCall + pythonArguments);
  
  Serial.println(systemCall);
  system(systemCall.buffer);
}

// Snagged from Arduino's ethernet page.
void setupEthernet()
{
  Serial.println("Setting up Ethernet...");
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("Failed to configure Ethernet using DHCP"));
    // no point in carrying on, so do nothing forevermore:
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip);
  }
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());
  // give the Ethernet shield a second to initialize:
  delay(1000);
}
